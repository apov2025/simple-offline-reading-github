/* SPDX-License-Identifier: Apache-2.0
 * 中文：校时调度、可信来源与清理边界回归。/ English: Sync scheduling, trusted provenance and cleanup boundary tests.
 */
#define READER_CLOCK_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include "../main/board/reader_clock.c"
static int64_t now_ms;
static clock_record_t disk;
static bool disk_present, rtc_valid, rtc_write_ok, save_ok, launch_ok, usb_busy;
static uint32_t rtc_epoch, rtc_boot;
static int launches, rtc_writes, saves, pauses;
static bool release_on_pause;
static int64_t clock_now_ms(void) { return now_ms; }
static bool clock_load(void *out,size_t size) { assert(size==sizeof(disk));if(disk_present)memcpy(out,&disk,size);return disk_present; }
static bool clock_save(const void *data,size_t size) { assert(size==sizeof(disk));++saves;if(!save_ok)return false;memcpy(&disk,data,size);disk_present=true;return true; }
static bool clock_rtc_read(uint32_t *epoch,uint32_t *boot) { if(!rtc_valid)return false;*epoch=rtc_epoch;*boot=rtc_boot;return true; }
static bool clock_rtc_write(uint32_t epoch,uint32_t *boot) { ++rtc_writes;if(!rtc_write_ok)return false;rtc_epoch=epoch;*boot=rtc_boot;return true; }
static bool clock_launch(void) { ++launches;return launch_ok; }
static void clock_pause(void) { ++pauses;now_ms+=10;if(release_on_pause)clock_complete(READER_CLOCK_CANCELLED,0,0); }
static bool clock_usb_busy(void) { return usb_busy; }
static void reset(void) {
    memset(&s_clock,0,sizeof(s_clock));memset(&disk,0,sizeof(disk));now_ms=0;
    disk_present=rtc_valid=usb_busy=false;rtc_write_ok=save_ok=launch_ok=true;
    rtc_epoch=1800000000;rtc_boot=7;launches=rtc_writes=saves=pauses=0;release_on_pause=true;
}
static void successful_sync(void) { clock_complete(READER_CLOCK_SUCCESS,0,1800000000);reader_clock_tick(); }
int main(void) {
    reset();reader_clock_init();assert(launches==1);reader_clock_init();assert(launches==1);
    int64_t epoch=99;assert(!reader_clock_trusted_epoch(&epoch)&&epoch==99);
    char date[16]="bad",hm[8]="bad";assert(!reader_clock_format(date,sizeof(date),hm,sizeof(hm))&&!date[0]&&!hm[0]);
    successful_sync();assert(rtc_writes==1&&disk_present);reader_clock_tick();assert(rtc_writes==1);
    assert(reader_clock_trusted_epoch(&epoch)&&epoch==1800000000);
    now_ms=123000;assert(reader_clock_trusted_epoch(&epoch)&&epoch==1800000123);
    assert(reader_clock_format(date,sizeof(date),hm,sizeof(hm)));assert(strlen(date)==10&&strlen(hm)==5);
    assert(reader_clock_cancel());assert(launches==1);reader_clock_init();assert(launches==1);
    // 软睡后PMU持续走时，同日不重连；次日首次开机仅启动一次。/ PMU continues after soft sleep: no second same-day sync; one sync at next-day boot.
    clock_record_t saved=disk;reset();disk=saved;disk_present=rtc_valid=true;rtc_epoch=saved.sync_epoch+100;
    reader_clock_init();assert(!launches&&reader_clock_trusted_epoch(&epoch)&&epoch==rtc_epoch);
    reset();disk=saved;disk_present=rtc_valid=true;rtc_epoch=saved.sync_epoch+86400;
    reader_clock_init();assert(launches==1);assert(disk.attempt_day==(rtc_epoch+480*60)/86400);
    clock_complete(READER_CLOCK_TIMEOUT,1,0);reader_clock_tick();assert(reader_clock_trusted_epoch(&epoch));
    saved=disk;reset();disk=saved;disk_present=rtc_valid=true;rtc_epoch=saved.sync_epoch+86400;
    reader_clock_init();assert(!launches);
    // 工厂假日期/PMU复位/旧NVS都不冒充当前时间。/ Factory dates, PMU resets and old NVS records never masquerade as current time.
    reset();rtc_valid=true;rtc_epoch=1704067200;reader_clock_init();assert(!reader_clock_trusted_epoch(&epoch)&&launches==1);
    reset();disk=saved;disk_present=rtc_valid=true;rtc_boot=8;reader_clock_init();assert(!reader_clock_trusted_epoch(&epoch));
    reset();disk=saved;disk_present=true;reader_clock_init();assert(!reader_clock_trusted_epoch(&epoch));
    reset();disk=saved;disk_present=rtc_valid=true;rtc_epoch=(uint32_t)saved.sync_epoch-1;reader_clock_init();assert(!reader_clock_trusted_epoch(&epoch));
    // 取消等待有界，尚未释放时不可谎报成功。/ Cancellation has a deadline and never claims cleanup before release.
    reset();reader_clock_init();release_on_pause=false;assert(!reader_clock_cancel()&&pauses==50);
    reader_clock_status_t status;reader_clock_get_status(&status);assert(status.active);
    release_on_pause=true;assert(reader_clock_cancel());reader_clock_get_status(&status);assert(!status.active&&status.state==READER_CLOCK_CANCELLED);
    reset();reader_clock_init();assert(!reader_clock_request());assert(launches==1);assert(reader_clock_cancel());assert(reader_clock_request());assert(launches==2);
    reset();usb_busy=true;reader_clock_init();assert(!launches);reader_clock_get_status(&status);assert(status.state==READER_CLOCK_BUSY);
    reset();launch_ok=false;reader_clock_init();reader_clock_get_status(&status);assert(!status.active&&status.state==READER_CLOCK_ERROR);
    reset();reader_clock_init();rtc_write_ok=false;successful_sync();reader_clock_get_status(&status);assert(status.trusted&&!status.rtc_retained&&status.state==READER_CLOCK_ERROR);
    reset();reader_clock_init();save_ok=false;successful_sync();reader_clock_get_status(&status);assert(status.trusted&&!status.rtc_retained&&status.state==READER_CLOCK_ERROR);
    reset();reader_clock_init();clock_complete(READER_CLOCK_SUCCESS,0,1704067200);reader_clock_cancel();reader_clock_tick();assert(!rtc_writes);reader_clock_get_status(&status);assert(status.state==READER_CLOCK_CANCELLED);
    reset();reader_clock_init();clock_complete(READER_CLOCK_NO_WIFI,0,0);reader_clock_tick();reader_clock_get_status(&status);assert(status.state==READER_CLOCK_NO_WIFI&&!status.active);
    assert(!reader_clock_set_timezone(841));assert(reader_clock_set_timezone(-300));reader_clock_get_status(&status);assert(status.timezone_minutes==-300);
    puts("reader_clock: provenance, daily boot policy, bounded cancellation and formatting passed");
}
