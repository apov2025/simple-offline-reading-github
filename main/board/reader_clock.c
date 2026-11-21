/* SPDX-License-Identifier: Apache-2.0
 * 中文：短时STA/SNTP校时，PMU可信时钟与每日开机调度；网络任务不访问PMU。
 * English: Short STA/SNTP sessions, trusted PMU time and daily boot scheduling; the network worker never accesses PMU.
 * 冻结：不启HTTP、不伪造日期、不同步于每次唤醒；WiFi释放前不报告取消完成。
 * Frozen: No HTTP, invented date or sync on every wake; cancellation completes only after radio cleanup.
 */
#include "reader_clock.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    uint32_t version, boot_id;
    int64_t sync_epoch, attempt_day;
    int timezone_minutes;
} clock_record_t;
static struct {
    bool initialized, running, cancel, pending, cleanup_blocked;
    reader_clock_state_t state;
    int last_error;
    bool trusted, retained;
    int64_t base_epoch, base_ms, result_epoch, result_ms;
    clock_record_t record;
} s_clock;

#if defined(READER_CLOCK_HOST_TEST)
#include "reader_clock_test_platform.h"
#elif defined(ESP_PLATFORM) && !READER_METALIO
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs.h"
#include "read_pico_pmu.h"
#include "read_pico_transfer.h"
#include "reader_usb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE s_clock_lock=portMUX_INITIALIZER_UNLOCKED;
#define CLOCK_LOCK() portENTER_CRITICAL(&s_clock_lock)
#define CLOCK_UNLOCK() portEXIT_CRITICAL(&s_clock_lock)
static int64_t clock_now_ms(void) { return esp_timer_get_time()/1000; }
static bool clock_load(void *out,size_t size) {
    nvs_handle_t h;
    if(nvs_open("rp_clock",NVS_READONLY,&h)!=ESP_OK)return false;
    size_t actual=size;
    esp_err_t err=nvs_get_blob(h,"record",out,&actual);nvs_close(h);
    return err==ESP_OK && actual==size;
}
static bool clock_save(const void *data,size_t size) {
    nvs_handle_t h;
    if(nvs_open("rp_clock",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t err=nvs_set_blob(h,"record",data,size);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);return err==ESP_OK;
}
static bool clock_rtc_read(uint32_t *epoch,uint32_t *boot) {
    if(!read_pico_pmu_ready() || read_pico_pmu_cmd(PMU_CMD_TIME_GET,NULL,0)!=ESP_OK)return false;
    const pmu_snapshot_t *p=read_pico_pmu_get();
    if(!p || !p->time_ok || !p->time_synced || p->last_op_status!=PMU_STATUS_OK)return false;
    *epoch=p->unix_sec;*boot=p->boot_id;return true;
}
static bool clock_rtc_write(uint32_t epoch,uint32_t *boot) {
    uint8_t payload[4]={(uint8_t)epoch,(uint8_t)(epoch>>8),(uint8_t)(epoch>>16),(uint8_t)(epoch>>24)};
    if(!read_pico_pmu_ready() || read_pico_pmu_cmd(PMU_CMD_TIME_SYNC,payload,4)!=ESP_OK)return false;
    uint32_t actual;
    return clock_rtc_read(&actual,boot) && actual>=epoch && actual-epoch<=3;
}
static void clock_worker(void *arg);
static bool clock_launch(void) { return xTaskCreate(clock_worker,"reader_clock",6144,NULL,3,NULL)==pdPASS; }
static void clock_pause(void) { vTaskDelay(pdMS_TO_TICKS(10)); }
static bool clock_usb_busy(void) { return reader_usb_active(); }
#else
#define CLOCK_LOCK() ((void)0)
#define CLOCK_UNLOCK() ((void)0)
static int64_t clock_now_ms(void) { return 0; }
static bool clock_load(void *out,size_t size) { (void)out;(void)size;return false; }
static bool clock_save(const void *data,size_t size) { (void)data;(void)size;return false; }
static bool clock_rtc_read(uint32_t *epoch,uint32_t *boot) { (void)epoch;(void)boot;return false; }
static bool clock_rtc_write(uint32_t epoch,uint32_t *boot) { (void)epoch;(void)boot;return false; }
static bool clock_launch(void) { return false; }
static void clock_pause(void) {}
static bool clock_usb_busy(void) { return true; }
#endif

static bool valid_epoch(int64_t epoch) { return epoch>=1577836800 && epoch<4102444800LL; }
static int64_t local_day(int64_t epoch) { return (epoch+(int64_t)s_clock.record.timezone_minutes*60)/86400; }
static void clock_complete(reader_clock_state_t state,int err,int64_t epoch) {
    int64_t now=clock_now_ms();
    CLOCK_LOCK();
    if(s_clock.cancel) { state=READER_CLOCK_CANCELLED;epoch=0; }
    if(s_clock.cleanup_blocked) { state=READER_CLOCK_ERROR;epoch=0; }
    s_clock.state=state==READER_CLOCK_SUCCESS ? READER_CLOCK_SYNCING : state;s_clock.last_error=err;
    s_clock.result_epoch=epoch;s_clock.result_ms=now;
    s_clock.pending=state==READER_CLOCK_SUCCESS;
    s_clock.running=false;
    CLOCK_UNLOCK();
}
bool reader_clock_trusted_epoch(int64_t *epoch) {
    if(!epoch || !s_clock.trusted)return false;
    int64_t elapsed=clock_now_ms()-s_clock.base_ms;
    if(elapsed<0)return false;
    int64_t now=s_clock.base_epoch+elapsed/1000;
    if(!valid_epoch(now))return false;
    *epoch=now;return true;
}
void reader_clock_get_status(reader_clock_status_t *out) {
    if(!out)return;
    int64_t epoch;
    bool trusted=reader_clock_trusted_epoch(&epoch);
    CLOCK_LOCK();
    *out=(reader_clock_status_t){.state=s_clock.state,.active=s_clock.running || s_clock.pending || s_clock.cleanup_blocked,
        .trusted=trusted,.rtc_retained=s_clock.retained,.last_error=s_clock.last_error,
        .timezone_minutes=s_clock.record.timezone_minutes};
    CLOCK_UNLOCK();
}
void reader_clock_init(void) {
    if(s_clock.initialized)return;
    s_clock.initialized=true;
    s_clock.record=(clock_record_t){.version=1,.attempt_day=-1,.timezone_minutes=480};
    clock_record_t record={0};
    if(clock_load(&record,sizeof(record)) && record.version==1 &&
       record.timezone_minutes>=-720 && record.timezone_minutes<=840)s_clock.record=record;
    uint32_t rtc=0,boot=0;
    if(clock_rtc_read(&rtc,&boot) && valid_epoch(rtc) && valid_epoch(s_clock.record.sync_epoch) &&
       s_clock.record.boot_id==boot && rtc>=s_clock.record.sync_epoch) {
        s_clock.trusted=s_clock.retained=true;s_clock.base_epoch=rtc;s_clock.base_ms=clock_now_ms();
    }
    if(!s_clock.trusted || s_clock.record.attempt_day!=local_day(rtc))reader_clock_request();
}
bool reader_clock_request(void) {
    if(!s_clock.initialized)return false;
    CLOCK_LOCK();bool running=s_clock.running || s_clock.pending || s_clock.cleanup_blocked;CLOCK_UNLOCK();
    if(running)return false;
    if(clock_usb_busy()) { s_clock.state=READER_CLOCK_BUSY;return false; }
    int64_t epoch;
    if(reader_clock_trusted_epoch(&epoch)) {
        s_clock.record.attempt_day=local_day(epoch);
        (void)clock_save(&s_clock.record,sizeof(s_clock.record));
    }
    CLOCK_LOCK();
    s_clock.running=true;s_clock.cancel=s_clock.pending=false;
    s_clock.state=READER_CLOCK_CONNECTING;s_clock.last_error=0;
    CLOCK_UNLOCK();
    if(!clock_launch()) { clock_complete(READER_CLOCK_ERROR,-1,0);return false; }
    return true;
}
bool reader_clock_cancel(void) {
    CLOCK_LOCK();s_clock.cancel=true;bool pending=s_clock.pending;s_clock.pending=false;bool running=s_clock.running;CLOCK_UNLOCK();
    // 最多50次短等待，不把联网总超时传给页面切换。/ At most 50 short waits; never impose the network deadline on page switching.
    for(unsigned i=0;running && i<50;++i) {
        clock_pause();CLOCK_LOCK();running=s_clock.running;CLOCK_UNLOCK();
    }
    if(!running && !s_clock.cleanup_blocked && (pending || (s_clock.state!=READER_CLOCK_IDLE && s_clock.state!=READER_CLOCK_SUCCESS)))
        s_clock.state=READER_CLOCK_CANCELLED;
    return !running && !s_clock.cleanup_blocked;
}
void reader_clock_tick(void) {
    CLOCK_LOCK();bool pending=s_clock.pending;int64_t epoch=s_clock.result_epoch,when=s_clock.result_ms;s_clock.pending=false;CLOCK_UNLOCK();
    if(!pending)return;
    int64_t now=clock_now_ms();
    if(!valid_epoch(epoch) || now<when) { s_clock.state=READER_CLOCK_ERROR;s_clock.last_error=-1;return; }
    epoch+=(now-when)/1000;
    if(!valid_epoch(epoch)) { s_clock.state=READER_CLOCK_ERROR;s_clock.last_error=-1;return; }
    s_clock.base_epoch=epoch;s_clock.base_ms=now;s_clock.trusted=true;s_clock.retained=false;
    uint32_t boot=0;
    // PMU命令共享序列号，必须由调用主线程串行执行；仅成功校时后一次约60ms。
    // PMU commands share sequence numbers: serialize on the calling UI owner, once after sync (~60ms).
    if(!clock_rtc_write((uint32_t)epoch,&boot)) { s_clock.state=READER_CLOCK_ERROR;s_clock.last_error=-1;return; }
    s_clock.record.version=1;s_clock.record.boot_id=boot;s_clock.record.sync_epoch=epoch;
    s_clock.record.attempt_day=local_day(epoch);
    if(!clock_save(&s_clock.record,sizeof(s_clock.record))) { s_clock.state=READER_CLOCK_ERROR;s_clock.last_error=-1;return; }
    s_clock.retained=true;s_clock.state=READER_CLOCK_SUCCESS;s_clock.last_error=0;
}
bool reader_clock_format(char *date,size_t date_size,char *clock,size_t clock_size) {
    if(date && date_size)date[0]=0;
    if(clock && clock_size)clock[0]=0;
    int64_t epoch;
    if(!date || date_size<11 || !clock || clock_size<6 || !reader_clock_trusted_epoch(&epoch))return false;
    time_t local=(time_t)(epoch+(int64_t)s_clock.record.timezone_minutes*60);
    struct tm tm;
    if(!gmtime_r(&local,&tm))return false;
    return strftime(date,date_size,"%Y-%m-%d",&tm)>0 && strftime(clock,clock_size,"%H:%M",&tm)>0;
}
bool reader_clock_set_timezone(int minutes) {
    if(minutes< -720 || minutes>840)return false;
    clock_record_t next=s_clock.record;next.timezone_minutes=minutes;
    if(!clock_save(&next,sizeof(next)))return false;
    s_clock.record=next;return true;
}

#if defined(ESP_PLATFORM) && !READER_METALIO && !defined(READER_CLOCK_HOST_TEST)
static bool s_net_ready,s_net_failed;
static void clock_wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;(void)data;
    CLOCK_LOCK();
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP)s_net_ready=true;
    if((base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) || (base==IP_EVENT && id==IP_EVENT_STA_LOST_IP)) {
        s_net_ready=false;s_net_failed=true;
    }
    CLOCK_UNLOCK();
}
static void erase(void *data,size_t size) { volatile unsigned char *p=data;while(size--)*p++=0; }
static void clock_worker(void *arg) {
    (void)arg;
    char ssid[33]={0},password[65]={0};
    wifi_config_t config={0};
    bool lease=false,loop_owned=false,wifi=false,started=false,sntp=false,log_changed=false;
    esp_log_level_t previous_wifi_log=ESP_LOG_NONE;
    esp_netif_t *netif=NULL;
    esp_event_handler_instance_t wifi_event=NULL,ip_event=NULL;
    reader_clock_state_t state=READER_CLOCK_ERROR;
    int64_t epoch=0,deadline=clock_now_ms()+25000;
    CLOCK_LOCK();bool cancelled=s_clock.cancel;CLOCK_UNLOCK();
    esp_err_t err=ESP_OK;
    if(cancelled) { state=READER_CLOCK_CANCELLED;goto cleanup; }
    err=read_pico_transfer_clock_acquire(ssid,password);
    if(err!=ESP_OK) {
        state=err==ESP_ERR_NOT_FOUND ? READER_CLOCK_NO_WIFI : err==ESP_ERR_INVALID_STATE ? READER_CLOCK_BUSY : READER_CLOCK_ERROR;
        goto cleanup;
    }
    lease=true;
    err=esp_netif_init();if(err!=ESP_OK)goto cleanup;
    err=esp_event_loop_create_default();
    if(err==ESP_OK)loop_owned=true;else if(err!=ESP_ERR_INVALID_STATE)goto cleanup;
    netif=esp_netif_create_default_wifi_sta();if(!netif){err=ESP_ERR_NO_MEM;goto cleanup;}
    // 驱动默认INFO会打印站点MAC；短时校时会话仅保留错误级日志。/ Driver INFO logs include station MAC; keep only errors during a clock session.
    previous_wifi_log=esp_log_level_get("wifi");esp_log_level_set("wifi",ESP_LOG_ERROR);log_changed=true;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init);if(err!=ESP_OK)goto cleanup;wifi=true;
    CLOCK_LOCK();s_net_ready=s_net_failed=false;CLOCK_UNLOCK();
    err=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,clock_wifi_event,NULL,&wifi_event);if(err!=ESP_OK)goto cleanup;
    err=esp_event_handler_instance_register(IP_EVENT,ESP_EVENT_ANY_ID,clock_wifi_event,NULL,&ip_event);if(err!=ESP_OK)goto cleanup;
    memcpy(config.sta.ssid,ssid,strlen(ssid));memcpy(config.sta.password,password,strlen(password));
    config.sta.threshold.authmode=password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable=true;config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    erase(ssid,sizeof(ssid));erase(password,sizeof(password));
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(err!=ESP_OK)goto cleanup;
    err=esp_wifi_set_mode(WIFI_MODE_STA);if(err!=ESP_OK)goto cleanup;
    err=esp_wifi_set_config(WIFI_IF_STA,&config);erase(&config,sizeof(config));if(err!=ESP_OK)goto cleanup;
    err=esp_wifi_start();if(err!=ESP_OK)goto cleanup;started=true;
    err=esp_wifi_connect();if(err!=ESP_OK)goto cleanup;
    for(;;) {
        CLOCK_LOCK();bool cancel=s_clock.cancel,ready=s_net_ready,failed=s_net_failed;CLOCK_UNLOCK();
        if(cancel) { state=READER_CLOCK_CANCELLED;err=ESP_OK;break; }
        if(clock_now_ms()>=deadline) { state=READER_CLOCK_TIMEOUT;err=ESP_ERR_TIMEOUT;break; }
        if(failed) { state=READER_CLOCK_ERROR;err=ESP_FAIL;break; }
        if(ready && !sntp) {
            esp_sntp_config_t ntp=ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
            err=esp_netif_sntp_init(&ntp);if(err!=ESP_OK)break;
            sntp=true;CLOCK_LOCK();s_clock.state=READER_CLOCK_SYNCING;CLOCK_UNLOCK();
        }
        if(sntp && esp_netif_sntp_sync_wait(0)==ESP_OK) {
            epoch=(int64_t)time(NULL);
            if(valid_epoch(epoch)){state=READER_CLOCK_SUCCESS;err=ESP_OK;}else err=ESP_ERR_INVALID_RESPONSE;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
cleanup:
    erase(ssid,sizeof(ssid));erase(password,sizeof(password));erase(&config,sizeof(config));
    if(sntp)esp_netif_sntp_deinit();
    // 清理瞬态失败最多重试三次；仍失败则保留租约，绝不向传书谎报无线已释放。
    // Retry transient teardown failures three times; persistent failure retains the lease rather than claiming radio release to transfer.
    for(unsigned i=0;started && i<3;++i) {
        esp_err_t e=esp_wifi_stop();
        if(e==ESP_OK)started=false;
        else { err=e;if(i<2)vTaskDelay(pdMS_TO_TICKS(100)); }
    }
    bool cleaned=!started;
    if(wifi_event && esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event)!=ESP_OK)cleaned=false;
    if(ip_event && esp_event_handler_instance_unregister(IP_EVENT,ESP_EVENT_ANY_ID,ip_event)!=ESP_OK)cleaned=false;
    for(unsigned i=0;cleaned && wifi && i<3;++i) {
        esp_err_t e=esp_wifi_deinit();
        if(e==ESP_OK)wifi=false;
        else { err=e;if(i<2)vTaskDelay(pdMS_TO_TICKS(100)); }
    }
    if(wifi)cleaned=false;
    if(cleaned && netif)esp_netif_destroy_default_wifi(netif);
    if(cleaned && loop_owned && esp_event_loop_delete_default()!=ESP_OK)cleaned=false;
    if(log_changed && cleaned)esp_log_level_set("wifi",previous_wifi_log);
    if(lease && cleaned)read_pico_transfer_clock_release();
    CLOCK_LOCK();s_clock.cleanup_blocked=!cleaned;CLOCK_UNLOCK();
    if(!cleaned){state=READER_CLOCK_ERROR;if(err==ESP_OK)err=ESP_FAIL;}
    clock_complete(state,err,epoch);
    vTaskDelete(NULL);
}
#endif
