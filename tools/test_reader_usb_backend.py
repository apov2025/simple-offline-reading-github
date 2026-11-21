#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the firmware MSC callbacks against a sector array and a delayed write."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/usb-backend-test'
out.mkdir(parents=True, exist_ok=True)
for include in ('esp_attr.h', 'esp_err.h', 'sdmmc_cmd.h', 'freertos/FreeRTOS.h', 'freertos/task.h',
                'hal/usb_serial_jtag_ll.h', 'read_pico_board.h', 'tinyusb_default_config.h'):
    header = out / include
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text('#pragma once\n')
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#define DMA_ATTR __attribute__((aligned(4)))
typedef int esp_err_t;
enum {ESP_OK,ESP_ERR_INVALID_STATE,ESP_ERR_NOT_SUPPORTED,ESP_FAIL};
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) assert(!pthread_mutex_lock(p))
#define portEXIT_CRITICAL(p) assert(!pthread_mutex_unlock(p))
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(int ticks) {(void)ticks;usleep(1000);}
typedef struct {struct {uint32_t capacity; uint16_t sector_size;} csd;} sdmmc_card_t;
typedef struct {int id;} tinyusb_event_t;
typedef struct {void (*cb)(tinyusb_event_t*,void*);} tinyusb_config_t;
#define TINYUSB_DEFAULT_CONFIG(fn) ((tinyusb_config_t){.cb=fn})
enum {TINYUSB_EVENT_ATTACHED, DETACHED, SUSPEND};
enum {SCSI_SENSE_NOT_READY,SCSI_SENSE_MEDIUM_ERROR,SCSI_SENSE_ILLEGAL_REQUEST};
static sdmmc_card_t card={{32,512}};
static unsigned char disk[16384];
static bool present=true, claimed, pad=true, external, write_error, install_error, uninstall_error;
static atomic_bool hold_write, writing, stop_done;
static int card_stops, uninstall_calls;
static bool card_stop_error,read_error,masked_partial,masked_corrupt;
static unsigned read_calls,write_calls;
static bool masked_empty;
static size_t corrupt_offset=SIZE_MAX;
static atomic_bool hold_read,reading;
static tinyusb_config_t cfg;
static bool read_pico_sd_present(void){return present;}
static bool usb_serial_jtag_ll_phy_is_pad_enabled(void){return pad;}
static void usb_serial_jtag_ll_phy_enable_pad(bool on){pad=on;}
static void usb_serial_jtag_ll_phy_enable_external(bool on){external=on;}
static int tinyusb_driver_install(const tinyusb_config_t *c){assert(!pad);cfg=*c;external=true;return install_error?ESP_FAIL:ESP_OK;}
static int tinyusb_driver_uninstall(void){assert(!writing);uninstall_calls++;return uninstall_error?ESP_FAIL:ESP_OK;}
static void tud_msc_set_sense(uint8_t lun,int key,int asc,int q){(void)lun;(void)key;(void)asc;(void)q;}
bool read_pico_sd_usb_claimed(void){return claimed;}
int read_pico_sd_claim_usb(sdmmc_card_t** c){if(!present)return ESP_FAIL;claimed=true;*c=&card;return ESP_OK;}
int read_pico_sd_end_usb(void){assert(!writing);card_stops++;if(card_stop_error)return ESP_FAIL;claimed=false;return ESP_OK;}
static int sdmmc_get_status(sdmmc_card_t* c){assert(c==&card);return ESP_OK;}
static int sdmmc_read_sectors(sdmmc_card_t* c,void* b,size_t block,size_t n){
    assert(c==&card && block+n<=32);++read_calls;if(read_error)return ESP_FAIL;
    reading=true;while(hold_read)usleep(1000);
    memcpy(b,disk+block*512,n*512);reading=false;return ESP_OK;
}
static int sdmmc_write_sectors(sdmmc_card_t* c,const void* b,size_t block,size_t n){
    assert(c==&card && block+n<=32 && n<=8);++write_calls;if(write_error)return ESP_FAIL;writing=true;
    while(hold_write)usleep(1000);
    if(!masked_empty)memcpy(disk+block*512,b,(masked_partial && n>1?n-1:n)*512);
    if(masked_corrupt && n>1)disk[(block+n)*512-1]^=1;
    if(corrupt_offset!=SIZE_MAX && n>1)disk[block*512+corrupt_offset]^=1;
    writing=false;return ESP_OK;
}
#include "../../main/board/reader_usb.c"
static uint8_t payload[12288], result[12288];
static void *writer(void *p){(void)p;assert(tud_msc_write10_cb(0,0,0,payload,512)==512);return NULL;}
static void *batch_writer(void *p){(void)p;assert(tud_msc_write10_cb(0,0,0,payload,4096)==4096);return NULL;}
static void *stopper(void *p){(void)p;assert(reader_usb_stop_confirmed()==ESP_OK);stop_done=true;return NULL;}
static void event(int id){tinyusb_event_t e={id};cfg.cb(&e,NULL);}
int main(int argc,char** argv){
    assert(argc==2);int scenario=atoi(argv[1]);
    if(scenario==1)present=false;
    if(scenario==2)install_error=true;
    int err=reader_usb_start();
    if(scenario==1){assert(err!=ESP_OK && !reader_usb_active());return 0;}
    assert(reader_usb_active());
    if(scenario==2){assert(err!=ESP_OK && reader_usb_state()==READER_USB_ERROR);
        assert(reader_usb_stop()!=ESP_OK && reader_usb_requires_restart() && reader_usb_active());return 0;}
    assert(err==ESP_OK && reader_usb_state()==READER_USB_WAITING);
    if(scenario==3){assert(reader_usb_stop()==ESP_OK && pad && !external);return 0;}
    event(TINYUSB_EVENT_ATTACHED);
    assert(reader_usb_state()==READER_USB_CONNECTED && reader_usb_stop()==ESP_ERR_INVALID_STATE);
    event(DETACHED);event(SUSPEND);assert(reader_usb_stop()==ESP_ERR_INVALID_STATE);
    if(scenario==4){present=false;assert(reader_usb_state()==READER_USB_ERROR);present=true;
        assert(!tud_msc_test_unit_ready_cb(0) && reader_usb_stop()==ESP_ERR_INVALID_STATE);
        assert(tud_msc_start_stop_cb(0,0,false,true) && reader_usb_stop()==ESP_OK);return 0;}
    if(scenario==5){write_error=true;assert(tud_msc_write10_cb(0,0,0,payload,512)<0);
        assert(reader_usb_state()==READER_USB_ERROR && reader_usb_stop()==ESP_ERR_INVALID_STATE);
        assert(reader_usb_stop_confirmed()==ESP_OK);return 0;}
    if(scenario==6){pthread_t a,b;hold_write=true;assert(!pthread_create(&a,NULL,writer,NULL));
        while(!writing)usleep(1000);
        present=false;assert(reader_usb_state()==READER_USB_ERROR);
        assert(reader_usb_stop()==ESP_ERR_INVALID_STATE);
        assert(!pthread_create(&b,NULL,stopper,NULL));usleep(20000);
        assert(!stop_done);hold_write=false;pthread_join(a,NULL);pthread_join(b,NULL);
        assert(stop_done && pad && !external && card_stops==1);return 0;}
    if(scenario==7){uninstall_error=true;assert(reader_usb_stop_confirmed()==ESP_FAIL);
        assert(card_stops==0 && reader_usb_active() && reader_usb_requires_restart());
        assert(reader_usb_stop()!=ESP_OK && card_stops==0 && uninstall_calls==1);return 0;}
    if(scenario==8){card_stop_error=true;assert(reader_usb_stop_confirmed()!=ESP_OK);
        assert(reader_usb_requires_restart() && reader_usb_active() && pad && !external);
        assert(reader_usb_stop_confirmed()!=ESP_OK && card_stops==1 && uninstall_calls==1);return 0;}
    for(size_t i=0;i<sizeof(payload);i++)payload[i]=(uint8_t)(i*43+19);
    if(scenario>=9) {
        if(scenario==10)masked_partial=true;
        if(scenario==11)masked_corrupt=true;
        if(scenario==12)read_error=true;
        if(scenario==16)write_error=true;
        if(scenario==20)masked_empty=true;
        if(scenario==21)corrupt_offset=0;
        if(scenario==22)corrupt_offset=2048;
        if(scenario==24) {
            assert(tud_msc_write10_cb(0,31,0,payload,1024)<0 && !write_calls && !read_calls);
            assert(reader_usb_stop_confirmed()==ESP_OK);return 0;
        }
        if(scenario==13) {
            pthread_t a,b;hold_read=true;assert(!pthread_create(&a,NULL,batch_writer,NULL));
            while(!reading)usleep(1000);
            assert(!pthread_create(&b,NULL,stopper,NULL));usleep(20000);assert(!stop_done);
            hold_read=false;pthread_join(a,NULL);pthread_join(b,NULL);
            assert(stop_done && card_stops==1 && read_calls==1 && write_calls==1);return 0;
        }
        size_t length=scenario==14 || scenario==15 || scenario==10 || scenario==11 || scenario==12 || scenario==16 || scenario==20 || scenario==21 || scenario==22?8192:4096;
        if(scenario==17)length=1024;
        if(scenario==18)length=4608;
        if(scenario==19)length=8704;
        uint32_t lba=scenario==23?24:0;
        size_t offset=scenario==15?123:0;
        int32_t result_size=tud_msc_write10_cb(0,lba,offset,payload,(uint32_t)length);
        if(scenario==10 || scenario==11 || scenario==12 || scenario==16 || scenario==20 || scenario==21 || scenario==22) {
            assert(result_size<0 && reader_usb_state()==READER_USB_ERROR);
            if(scenario==16)assert(!read_calls);
            assert(!tud_msc_test_unit_ready_cb(0));
            if(scenario!=16)assert(write_calls==1 && read_calls==1);
            assert(reader_usb_stop_confirmed()==ESP_OK);return 0;
        }
        assert(result_size==(int32_t)length && !memcmp(disk+(size_t)lba*512+offset,payload,length));
        offset+=(size_t)lba*512;
        for(size_t i=0;i<sizeof(disk);i++)assert(disk[i]==(i>=offset && i<offset+length?payload[i-offset]:0));
        if(scenario!=15) {
            size_t remainder=length%4096;
            assert(write_calls==(length+4095)/4096);
            assert(read_calls==length/4096+(remainder>512));
        }
        assert(reader_usb_stop_confirmed()==ESP_OK);return 0;
    }
    assert(tud_msc_write10_cb(0,0,200,payload,1024)==1024);
    assert(tud_msc_read10_cb(0,0,200,result,1024)==1024 && !memcmp(payload,result,1024));
    for(size_t i=0;i<sizeof(disk);i++)
        assert(disk[i]==((i>=200 && i<1224)?payload[i-200]:0));
    assert(tud_msc_write10_cb(0,31,511,payload,2)<0);
    assert(tud_msc_write10_cb(1,0,0,payload,512)<0);
    uint8_t sync[16]={0x35};assert(tud_msc_scsi_cb(0,sync,NULL,0)==0);
    assert(tud_msc_start_stop_cb(0,0,false,true) && reader_usb_state()==READER_USB_EJECTED);
    event(TINYUSB_EVENT_ATTACHED);assert(tud_msc_write10_cb(0,0,0,payload,512)<0);
    assert(reader_usb_stop()==ESP_OK && !reader_usb_active() && card_stops==1 && pad && !external);
    assert(!reader_usb_requires_restart() && reader_usb_state()==READER_USB_OFF);
    assert(reader_usb_start()==ESP_OK && reader_usb_state()==READER_USB_WAITING);
    event(TINYUSB_EVENT_ATTACHED);assert(tud_msc_test_unit_ready_cb(0));
    assert(reader_usb_stop()==ESP_ERR_INVALID_STATE && !reader_usb_requires_restart());
    assert(reader_usb_stop_confirmed()==ESP_OK && !reader_usb_active() && card_stops==2);
}
'''
(out / 'test.c').write_text(source)
subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
                '-fsanitize=address,undefined', '-I'+str(out),
                '-I'+str(root/'components/read_pico/include'), str(out/'test.c'),
                '-o', str(out/'test')], check=True)
for scenario in range(25):
    subprocess.run([str(out/'test'), str(scenario)], check=True, timeout=8)
print('PASS: bounded verified batching, masked partial/corrupt writes, verification errors, busy verification stop; sector writes/readback, eject, suspend/detach, removal, startup/teardown errors, busy stop and PHY restoration')
