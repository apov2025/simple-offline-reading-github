/* SPDX-License-Identifier: Apache-2.0
 * 中文：运行真实传书网络租约及停止门，确保校时不会被传书路径拆掉。/ English: Exercise real transfer radio leases and stop gates so transfer cannot dismantle clock resources.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
typedef int esp_err_t;
enum {ESP_OK,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_NOT_FOUND,ESP_FAIL,READ_PICO_TRANSFER_STOPPED,WIFI_EVENT,IP_EVENT,ESP_EVENT_ANY_ID};
static int s_lock;
#define portENTER_CRITICAL(x) ((void)(x))
static void test_unlock(void);
#define portEXIT_CRITICAL(x) ((void)(x),test_unlock())
static bool s_wifi,s_started,s_loop_owned,s_config_busy,s_network_claimed,s_clock_claimed,s_stopping,s_upload_active;
static void *s_netif,*s_http,*s_events,*s_ip_events;
static char *s_buffer;
static struct {int state,sta_count;bool network_ready;char url[64];} s_status;
static struct {int dummy;} s_connection;
typedef struct {uint32_t version;char ssid[33],password[65];} transfer_credentials_t;
static int load_error,cleanups;
static bool saved=true;
static esp_err_t load_credentials(transfer_credentials_t*out){memset(out,0,sizeof(*out));if(saved){out->version=1;strcpy(out->ssid,"fixture");strcpy(out->password,"secret");}return load_error;}
static void clear_secret(void*p,size_t n){memset(p,0,n);}
static void httpd_stop(void*p){(void)p;++cleanups;}
static void esp_wifi_stop(void){++cleanups;}
static void esp_wifi_deinit(void){++cleanups;}
static void esp_event_handler_instance_unregister(int base,int id,void*h){(void)base;(void)id;(void)h;++cleanups;}
static void esp_netif_destroy_default_wifi(void*n){(void)n;++cleanups;}
static void esp_event_loop_delete_default(void){++cleanups;}
static bool admission_stop(bool *stopping,bool uploading){if(uploading)return false;*stopping=true;return true;}
static void read_pico_transfer_stop(void);
static bool inject_acquire;
static int injected_result;
esp_err_t read_pico_transfer_clock_acquire(char ssid[33],char password[65]);
static void test_unlock(void) {
    if(inject_acquire) {
        inject_acquire=false;
        char ssid[33],password[65];
        injected_result=read_pico_transfer_clock_acquire(ssid,password);
    }
}
#include "reader_clock_lease_under_test.h"
int main(void){
    char ssid[33],password[65];
    // 在空闲stop刚解锁时模拟worker抢租约；stop必须已占住网络直到清理完成。
    // Simulate worker admission at the first unlock of idle stop; cleanup must already hold the radio lease.
    inject_acquire=true;injected_result=-1;read_pico_transfer_stop();
    assert(injected_result==ESP_ERR_INVALID_STATE&&!s_network_claimed&&!s_clock_claimed);
    inject_acquire=true;injected_result=-1;assert(read_pico_transfer_try_stop_if_idle());
    assert(injected_result==ESP_ERR_INVALID_STATE&&!s_network_claimed&&!s_clock_claimed);
    assert(read_pico_transfer_clock_acquire(ssid,password)==ESP_OK);
    assert(s_clock_claimed&&s_network_claimed&&!strcmp(ssid,"fixture")&&!strcmp(password,"secret"));
    assert(!claim_network(false)&&!claim_config());
    assert(!read_pico_transfer_try_stop_if_idle());read_pico_transfer_stop();assert(s_clock_claimed&&!cleanups);
    read_pico_transfer_clock_release();assert(!s_network_claimed&&!s_clock_claimed);
    assert(claim_network(false));memset(ssid,'x',sizeof(ssid));memset(password,'x',sizeof(password));
    assert(read_pico_transfer_clock_acquire(ssid,password)==ESP_ERR_INVALID_STATE&&!ssid[0]&&!password[0]);
    read_pico_transfer_clock_release();assert(s_network_claimed);release_network();
    assert(claim_config());assert(read_pico_transfer_clock_acquire(ssid,password)==ESP_ERR_INVALID_STATE);release_config();
    s_clock_claimed=true;assert(!claim_network(false));s_clock_claimed=false;
    s_wifi=true;assert(!claim_network(true));s_wifi=false;
    s_http=(void*)1;assert(!claim_network(true));s_http=NULL;
    s_netif=(void*)1;assert(!claim_network(true));s_netif=NULL;
    saved=false;assert(read_pico_transfer_clock_acquire(ssid,password)==ESP_ERR_NOT_FOUND&&!s_network_claimed&&!password[0]);
    saved=true;load_error=ESP_FAIL;assert(read_pico_transfer_clock_acquire(ssid,password)==ESP_FAIL&&!s_network_claimed&&!password[0]);load_error=0;
    assert(read_pico_transfer_clock_acquire(NULL,password)==ESP_ERR_INVALID_ARG&&!s_network_claimed);
    assert(claim_network(false));s_wifi=s_started=s_loop_owned=true;s_netif=s_events=s_ip_events=s_http=(void*)1;
    assert(read_pico_transfer_try_stop_if_idle());assert(!s_network_claimed&&cleanups==7&&!s_wifi&&!s_netif&&!s_http);
    puts("reader_clock lease: manual transfer, scan/config guards, secret failure cleanup and owner-only stop passed");
}
