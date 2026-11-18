/* SPDX-License-Identifier: Apache-2.0
 * Metalio 刷屏出口，使用该面板的官方波形。/ Metalio presentation using its official panel waveform.
 */
#include "display.h"
#include "reader_waveform.h"
#include "app_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lcd_driver.h"
#include "epd_board_metalio_eink4_plus.h"
static bool power_fault;
static bool scan_fault;
static int soft_count;
static int pclk = 20;
static bool white_exit;
// Metalio 每次刷新立即关屏幕电源；不沿用 Pico 的空闲保电策略。
// Metalio powers the panel off after each update; do not retain Pico's idle-power policy.
void rails_keepalive(void) { epd_poweroff(); }
void rails_idle_check(int64_t now) { (void)now; }
static bool panel_begin(void) {
    // 恢复失败后物理画面未知，停止本次开机内的扫描，不能继续差分驱动。
    // Failed recovery leaves the physical image unknown; inhibit further scans until reboot.
    if(scan_fault)return false;
    epd_poweron();
    power_fault=!epd_board_metalio_eink4_plus_tps_ok();
    if(power_fault)epd_poweroff();
    return !power_fault;
}
void display_set_bulk_io(bool active) { (void)active; }
int display_pclk_mhz(void) { return pclk; }
void display_hold_white_exit(bool hold) { white_exit=hold; }
bool display_take_white_exit(void) { bool old=white_exit; white_exit=false; return old; }
static enum EpdDrawError draw(EpdiyHighlevelState* hl, const EpdWaveform* wf,
    enum EpdDrawMode mode, bool full, const EpdRect* area) {
    if ((mode & 15) == MODE_GC16) soft_count=0;
    else if (++soft_count >= APP_GC16_EVERY) { mode=MODE_GC16; full=true; soft_count=0; }
    if(!panel_begin())return EPD_DRAW_EMPTY_LINE_QUEUE;
    epd_hl_waveform(hl,wf);
    // 官方缺少区域全像素接口，首版灰阶定稿用整屏以保持物理基准。
    // The official backend lacks full-pixel area updates; settle gray with a full screen to preserve the physical baseline.
    enum EpdDrawError err = (full || (mode & 15)==MODE_GL16)
        ? epd_hl_update_screen_full(hl,mode,25)
        : area ? epd_hl_update_area(hl,mode,25,*area) : epd_hl_update_screen(hl,mode,25);
    epd_hl_waveform(hl,&READER_WAVEFORM);
    epd_poweroff(); return err;
}
enum EpdDrawError update_display_mode(EpdiyHighlevelState* hl, enum EpdDrawMode mode) { return draw(hl,&READER_WAVEFORM,mode,false,NULL); }
enum EpdDrawError update_display_full(EpdiyHighlevelState* hl) { return draw(hl,&READER_WAVEFORM,MODE_GC16,true,NULL); }
enum EpdDrawError update_display_white(EpdiyHighlevelState* hl) { epd_hl_set_all_white(hl); return update_display_full(hl); }
enum EpdDrawError update_display_with(EpdiyHighlevelState* hl,const EpdWaveform* wf,enum EpdDrawMode mode) { return draw(hl,wf,mode,false,NULL); }
enum EpdDrawError update_display_area_with(EpdiyHighlevelState* hl,const EpdWaveform* wf,enum EpdDrawMode mode,EpdRect area) { return draw(hl,wf,mode,false,&area); }
enum EpdDrawError update_display_from_white_with(EpdiyHighlevelState* hl,const EpdWaveform* wf,enum EpdDrawMode mode) {
    if(!panel_begin())return EPD_DRAW_EMPTY_LINE_QUEUE;
    epd_hl_waveform(hl,wf);
    enum EpdDrawError err=epd_hl_update_screen_from_white(hl,mode,25);
    epd_hl_waveform(hl,&READER_WAVEFORM); soft_count=0; epd_poweroff(); return err;
}
enum EpdDrawError update_display_from_white(EpdiyHighlevelState* hl) { return update_display_from_white_with(hl,&READER_WAVEFORM,MODE_GC16); }
void guard_draw_result(EpdiyHighlevelState* hl,enum EpdDrawError err) {
    if (!err || scan_fault) return;
    ESP_LOGW("metalio_display","refresh failed: %d",err);
    if ((err & EPD_DRAW_EMPTY_LINE_QUEUE) && !power_fault) {
        pclk=12; epd_lcd_set_pixel_clock_MHz(pclk);
        if(!panel_begin())return;
        // 清屏失败后物理基准未知，不继续假设白底重绘。
        // A failed clear leaves the physical baseline unknown; do not redraw as if white.
        epd_hl_waveform(hl,&READER_WAVEFORM);
        enum EpdDrawError recovered=epd_clear_checked();
        if(recovered==EPD_DRAW_SUCCESS)
            recovered=epd_hl_update_screen_from_white(hl,MODE_GC16,25);
        epd_poweroff();
        if(recovered!=EPD_DRAW_SUCCESS) {
            scan_fault=true;
            ESP_LOGW("metalio_display","recovery failed: %d; panel disabled until reboot",recovered);
        }
    }
}
