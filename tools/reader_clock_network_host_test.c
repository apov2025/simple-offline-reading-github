/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实联网任务与故障清理路径；替换SDK边界。/ English: Run the real network worker and failure cleanup paths with SDK boundary substitutes.
 */
#define READER_CLOCK_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../main/board/reader_clock.c"
typedef int esp_err_t;
typedef int esp_log_level_t;
enum {ESP_LOG_NONE,ESP_LOG_ERROR};
static esp_log_level_t esp_log_level_get(const char *tag){assert(!strcmp(tag,"wifi"));return ESP_LOG_NONE;}
static void esp_log_level_set(const char *tag,esp_log_level_t level){assert(!strcmp(tag,"wifi"));(void)level;}
typedef int esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef struct { int unused; } esp_netif_t;
typedef struct { int unused; } wifi_init_config_t;
typedef struct { struct { char ssid[32],password[64];struct {int authmode;} threshold;struct {bool capable;} pmf_cfg;int sae_pwe_h2e; } sta; } wifi_config_t;
typedef struct { const char *server; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) ((esp_sntp_config_t){server})
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NOT_FOUND=1,ESP_ERR_INVALID_STATE,ESP_ERR_NO_MEM,ESP_ERR_TIMEOUT,ESP_ERR_INVALID_RESPONSE};
enum {WIFI_EVENT=1,IP_EVENT,WIFI_EVENT_STA_DISCONNECTED,IP_EVENT_STA_LOST_IP,IP_EVENT_STA_GOT_IP,ESP_EVENT_ANY_ID,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_OPEN,WPA3_SAE_PWE_BOTH,WIFI_STORAGE_RAM,WIFI_MODE_STA,WIFI_IF_STA};
#define pdMS_TO_TICKS(x) (x)
static int64_t mock_now;
static int stage,fail_stage,acquire_error,release_count,stop_count,deinit_count,sntp_deinits,loop_deletes,netif_destroys,registrations,stop_failures,deinit_failures;
static bool lease,wifi_allocated,started,ntp_allocated,loop_allocated,netif_allocated,sync_result,never_ready,disconnect_now,cancel_on_delay;
static void (*event_cb)(void*,esp_event_base_t,int32_t,void*);
static int64_t clock_now_ms(void){return mock_now;}
static bool clock_load(void*out,size_t size){(void)out;(void)size;return false;}
static bool clock_save(const void*in,size_t size){(void)in;(void)size;return true;}
static bool clock_rtc_read(uint32_t*a,uint32_t*b){(void)a;(void)b;return false;}
static bool clock_rtc_write(uint32_t a,uint32_t*b){(void)a;(void)b;return true;}
static bool clock_launch(void){return true;}
static void clock_pause(void){}
static bool clock_usb_busy(void){return false;}
static int call(void){return ++stage==fail_stage?ESP_FAIL:ESP_OK;}
static int read_pico_transfer_clock_acquire(char ssid[33],char pwd[65]){if(acquire_error)return acquire_error;assert(!lease);lease=true;strcpy(ssid,"fixture");strcpy(pwd,"fixturepass");return ESP_OK;}
static void read_pico_transfer_clock_release(void){assert(lease&&!wifi_allocated&&!started&&!ntp_allocated&&!netif_allocated&&!loop_allocated&&!registrations);lease=false;++release_count;}
static int esp_netif_init(void){return call();}
static int esp_event_loop_create_default(void){int e=call();if(!e)loop_allocated=true;return e;}
static int esp_event_loop_delete_default(void){assert(loop_allocated);loop_allocated=false;++loop_deletes;return ESP_OK;}
static esp_netif_t *esp_netif_create_default_wifi_sta(void){if(call())return NULL;netif_allocated=true;return (esp_netif_t*)1;}
static void esp_netif_destroy_default_wifi(esp_netif_t*n){assert(n&&netif_allocated);netif_allocated=false;++netif_destroys;}
static int esp_wifi_init(const wifi_init_config_t*c){(void)c;int e=call();if(!e)wifi_allocated=true;return e;}
static int esp_wifi_deinit(void){assert(wifi_allocated&&!started);if(deinit_failures-->0)return ESP_FAIL;wifi_allocated=false;++deinit_count;return ESP_OK;}
static int esp_event_handler_instance_register(esp_event_base_t b,int id,void(*cb)(void*,esp_event_base_t,int32_t,void*),void*arg,esp_event_handler_instance_t*out){(void)b;(void)id;(void)arg;int e=call();if(!e){++registrations;*out=(void*)(long)registrations;event_cb=cb;}return e;}
static int esp_event_handler_instance_unregister(esp_event_base_t b,int id,esp_event_handler_instance_t h){(void)b;(void)id;assert(h&&registrations);--registrations;return ESP_OK;}
static int esp_wifi_set_storage(int mode){assert(mode==WIFI_STORAGE_RAM);return call();}
static int esp_wifi_set_mode(int mode){assert(mode==WIFI_MODE_STA);return call();}
static int esp_wifi_set_config(int mode,const wifi_config_t*c){assert(mode==WIFI_IF_STA&&!strcmp(c->sta.ssid,"fixture"));return call();}
static int esp_wifi_start(void){int e=call();if(!e)started=true;return e;}
static int esp_wifi_stop(void){assert(started);if(stop_failures-->0)return ESP_FAIL;started=false;++stop_count;return ESP_OK;}
static int esp_wifi_connect(void){return call();}
static int esp_netif_sntp_init(const esp_sntp_config_t*c){assert(c->server&&started);int e=call();if(!e)ntp_allocated=true;return e;}
static int esp_netif_sntp_sync_wait(int wait){assert(wait==0&&ntp_allocated);return sync_result?ESP_OK:ESP_ERR_TIMEOUT;}
static void esp_netif_sntp_deinit(void){assert(ntp_allocated);ntp_allocated=false;++sntp_deinits;}
static time_t test_time(time_t*out){time_t t=1800000000;if(out)*out=t;return t;}
#define time test_time
static void vTaskDelay(int ms){mock_now+=ms;if(cancel_on_delay)s_clock.cancel=true;if(!never_ready&&event_cb)event_cb(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);if(disconnect_now&&event_cb)event_cb(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);}
static void vTaskDelete(void*t){(void)t;}
#include "reader_clock_worker_under_test.h"
#undef time
static void reset(void){
    memset(&s_clock,0,sizeof(s_clock));s_clock.running=true;s_clock.state=READER_CLOCK_CONNECTING;
    mock_now=stage=fail_stage=acquire_error=release_count=stop_count=deinit_count=sntp_deinits=loop_deletes=netif_destroys=registrations=stop_failures=deinit_failures=0;
    lease=wifi_allocated=started=ntp_allocated=loop_allocated=netif_allocated=never_ready=disconnect_now=cancel_on_delay=false;sync_result=true;event_cb=NULL;
}
static void clean(void){assert(!s_clock.running&&!lease&&!wifi_allocated&&!started&&!ntp_allocated&&!netif_allocated&&!loop_allocated&&!registrations);}
int main(void){
    reset();clock_worker(NULL);clean();assert(s_clock.pending&&release_count==1&&stop_count==1&&sntp_deinits==1);int steps=stage;
    for(int i=1;i<=steps;++i){reset();fail_stage=i;clock_worker(NULL);clean();assert(s_clock.state==READER_CLOCK_ERROR&&!s_clock.pending&&release_count==1);}
    reset();never_ready=true;clock_worker(NULL);clean();assert(s_clock.state==READER_CLOCK_TIMEOUT&&mock_now==25000&&!sntp_deinits);
    reset();sync_result=false;clock_worker(NULL);clean();assert(s_clock.state==READER_CLOCK_TIMEOUT&&mock_now==25000&&sntp_deinits==1);
    reset();cancel_on_delay=true;clock_worker(NULL);clean();assert(s_clock.state==READER_CLOCK_CANCELLED&&mock_now==100);
    reset();disconnect_now=true;clock_worker(NULL);clean();assert(s_clock.state==READER_CLOCK_ERROR&&mock_now==100);
    reset();acquire_error=ESP_ERR_NOT_FOUND;clock_worker(NULL);clean();assert(s_clock.state==READER_CLOCK_NO_WIFI&&!stage&&!release_count);
    reset();acquire_error=ESP_ERR_INVALID_STATE;clock_worker(NULL);clean();assert(s_clock.state==READER_CLOCK_BUSY&&!stage&&!release_count);
    reset();stop_failures=deinit_failures=1;clock_worker(NULL);clean();assert(release_count==1);
    reset();stop_failures=3;clock_worker(NULL);assert(!s_clock.running&&s_clock.cleanup_blocked&&lease&&s_clock.state==READER_CLOCK_ERROR);assert(!reader_clock_cancel());
    puts("reader_clock network: real worker success, 25s deadlines, cancellation, ownership and every startup failure passed");
}
