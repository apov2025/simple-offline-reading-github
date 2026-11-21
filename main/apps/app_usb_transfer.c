/* SPDX-License-Identifier: Apache-2.0
 * USB传输独占页：先关阅读/字体，再导出卡，安全退出后恢复书库。/ Exclusive USB page: close reader/fonts, export SD, restore library after safe stop.
 * 冻结：不自动开启、不格式化；传输时不锁屏、不进入其它页。/ Frozen: manual only, never format; no lock or navigation during export.
 */
#include "app.h"
#include "ui_device_panel.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_gesture.h"
#include "ttf_font.h"
#include "book_source.h"
#include "display.h"
#include "esp_system.h"
#include "settings.h"
#include "../board/reader_media.h"
#include "../board/reader_waveform.h"
#include <string.h>
#include "../board/reader_usb.h"
#include "../board/reader_battery.h"

static bool usb_pending, usb_confirm, usb_returning, usb_restart_needed, usb_painted;
static bool usb_probe_slow;
static int64_t usb_probe_started, usb_probe_after;
static reader_usb_state_t usb_snapshot;
static esp_err_t usb_error;
static ui_battery_t usb_battery;
extern const app_desc_t app_reader;

static void usb_enter(app_ctx_t* ctx) {
    (void)ctx;
    book_close();
    reader_battery_sample(&usb_battery);
    usb_error=ttf_font_suspend_sd(true);
    usb_pending=usb_error==ESP_OK;
    usb_snapshot=usb_pending?READER_USB_OFF:READER_USB_ERROR;
    usb_confirm=usb_returning=usb_restart_needed=usb_painted=usb_probe_slow=false;
    display_set_bulk_io(true);
}
static void usb_leave(app_ctx_t* ctx) {
    (void)ctx;
    if(reader_usb_active() || usb_restart_needed)return;
    ttf_font_suspend_sd(false);
    display_set_bulk_io(false);
}
static void usb_render(app_ctx_t* ctx,uint8_t* fb) {
    (void)ctx;
    ui_clear_page(fb);
    ui_device_panel_draw(fb,&usb_battery,false,
        usb_pending?UI_DEVICE_USB_STARTING:usb_returning?UI_DEVICE_USB_ENDING:
        reader_usb_active()?UI_DEVICE_USB_ON:UI_DEVICE_USB_OFF,false);
    if(usb_restart_needed) {
        ui_text(fb,UI_MARGIN,440,34,"USB 未能完全关闭",EPD_DRAW_ALIGN_LEFT,false);
        ui_text(fb,UI_MARGIN,512,28,"传输已停止，重新启动后可返回书库。",EPD_DRAW_ALIGN_LEFT,false);
        ui_draw_round_rect(fb,(EpdRect){UI_MARGIN,640,ui_content_width(),88},14,0);
        ui_text_vc(fb,UI_LOCK_WIDTH/2,684,32,"重新启动",EPD_DRAW_ALIGN_CENTER,false);
        return;
    }
    if(usb_confirm) {
        ui_text(fb,UI_MARGIN,440,36,"先在电脑上弹出磁盘",EPD_DRAW_ALIGN_LEFT,false);
        ui_text(fb,UI_MARGIN,504,28,"若已停止复制并拔掉数据线，也可退出。",EPD_DRAW_ALIGN_LEFT,false);
        ui_text(fb,UI_MARGIN,548,28,"结束后返回书库。",EPD_DRAW_ALIGN_LEFT,false);
        ui_draw_round_rect(fb,(EpdRect){40,640,280,88},14,0x80);
        ui_draw_round_rect(fb,(EpdRect){344,640,300,88},14,0x80);
        ui_text_vc(fb,180,684,30,"取消",EPD_DRAW_ALIGN_CENTER,false);
        ui_text_vc(fb,494,684,30,"已弹出，结束",EPD_DRAW_ALIGN_CENTER,false);
        return;
    }
    const char* message=usb_pending?"正在准备 USB传输":usb_returning?"正在返回书库":
        usb_error!=ESP_OK?"未能开启或结束传输":
        usb_snapshot==READER_USB_WAITING?"请用 USB 连接电脑":
        usb_snapshot==READER_USB_CONNECTED?"可以在电脑上管理 SD 卡文件":
        usb_snapshot==READER_USB_EJECTED?"已弹出，可以结束 USB传输":
        usb_snapshot==READER_USB_ERROR?"SD 卡已移除或读写失败":"USB传输未开启";
    ui_text(fb,UI_MARGIN,440,34,message,EPD_DRAW_ALIGN_LEFT,false);
    if(usb_returning) {
        if(usb_probe_slow)ui_text(fb,UI_MARGIN,512,28,"正在等候 SD 卡就绪。",EPD_DRAW_ALIGN_LEFT,false);
    } else if(reader_usb_active()) {
        ui_text(fb,UI_MARGIN,512,28,"书籍放入 books，字体放入 fonts。",EPD_DRAW_ALIGN_LEFT,false);
        ui_text(fb,UI_MARGIN,560,28,"完成后先在电脑弹出，再点“结束”。",EPD_DRAW_ALIGN_LEFT,false);
        ui_text(fb,UI_MARGIN,608,28,"传输期间暂停阅读和锁屏。",EPD_DRAW_ALIGN_LEFT,false);
    } else if(!usb_pending && !usb_returning) {
        ui_text(fb,UI_MARGIN,512,28,"请确认已插入可正常读取的 SD 卡。",EPD_DRAW_ALIGN_LEFT,false);
        ui_text(fb,UI_LOCK_WIDTH/2,680,32,"返回书库",EPD_DRAW_ALIGN_CENTER,false);
    }
}
static bool usb_present(app_ctx_t* ctx,app_redraw_t redraw) {
    if(redraw!=APP_REDRAW_PAGE && redraw!=APP_REDRAW_FULL)return false;
    usb_render(ctx,ctx->fb);
    // 首帧覆盖书库，其后只刷新开关与提示所在区域。/ Cover the old library once, then refresh the switch and status region.
    EpdRect area=usb_painted?(EpdRect){0,216,UI_LOCK_WIDTH,540}:(EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT};
    enum EpdDrawError result=redraw==APP_REDRAW_FULL?update_display_full(ctx->hl):
        update_display_area_with(ctx->hl,&READER_WAVEFORM,MODE_GL16,area);
    guard_draw_result(ctx->hl,result);
    usb_painted=true;
    return true;
}
static app_redraw_t usb_finish(app_ctx_t* ctx,bool confirmed) {
    if(usb_pending || usb_returning || usb_restart_needed)return APP_REDRAW_NONE;
    if(reader_usb_active()) {
        usb_error=confirmed?reader_usb_stop_confirmed():reader_usb_stop();
        usb_restart_needed=reader_usb_requires_restart();
        if(usb_error==ESP_ERR_INVALID_STATE && !confirmed && !usb_restart_needed) {
            usb_error=ESP_OK;usb_confirm=true;return APP_REDRAW_PAGE;
        }
        usb_confirm=false;
        if(usb_error!=ESP_OK || usb_restart_needed)return APP_REDRAW_PAGE;
    }
    usb_returning=true;usb_confirm=false;
    usb_probe_started=usb_probe_after=ctx->now_ms;
    return APP_REDRAW_PAGE;
}
static app_redraw_t usb_gesture(app_ctx_t* ctx,const ui_gesture_event_t* ev) {
    if(ev->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    if(usb_restart_needed) {
        if(ev->x0>=UI_MARGIN && ev->x0<ui_content_right() && ev->y0>=640 && ev->y0<728)esp_restart();
        return APP_REDRAW_NONE;
    }
    if(usb_confirm) {
        if(ev->y0>=640 && ev->y0<728) {
            if(ev->x0>=40 && ev->x0<320) {usb_confirm=false;return APP_REDRAW_PAGE;}
            if(ev->x0>=344 && ev->x0<644)return usb_finish(ctx,true);
        }
        return APP_REDRAW_NONE;
    }
    if(ui_device_usb_hit(ev->x0,ev->y0,false)) {
        if(usb_pending || usb_returning)return APP_REDRAW_NONE;
        if(reader_usb_active())return usb_finish(ctx,false);
        // 失败后“开启”仍是重试，不误作退出。/ The Start action retries after failure instead of exiting.
        usb_error=ttf_font_suspend_sd(true);
        usb_pending=usb_error==ESP_OK;
        usb_snapshot=usb_pending?READER_USB_OFF:READER_USB_ERROR;
        return APP_REDRAW_PAGE;
    }
    if(!reader_usb_active() && ev->y0>=640 && ev->y0<728)return usb_finish(ctx,false);
    return APP_REDRAW_NONE;
}
static app_redraw_t usb_key(app_ctx_t* ctx,int key) {
    if(key==UI_KEY_2) {
        if(usb_confirm){usb_confirm=false;return APP_REDRAW_PAGE;}
        return usb_finish(ctx,false);
    }
    if(key==UI_KEY_3 && usb_snapshot!=READER_USB_CONNECTED)return APP_REDRAW_FULL;
    return APP_REDRAW_NONE;
}
static app_redraw_t usb_tick(app_ctx_t* ctx) {
    if(usb_restart_needed)return APP_REDRAW_NONE;
    if(usb_returning) {
        if(ctx->now_ms<usb_probe_after)return APP_REDRAW_NONE;
        usb_probe_after=ctx->now_ms+150;
        // 首次在返回提示上屏后启动；重复调用先检查工作线程，拔卡也必须等它结束。
        // Start after the notice is visible; the idempotent probe checks its worker before reporting removal.
        esp_err_t state=reader_media_start_probe();
        if(state==ESP_ERR_NOT_FINISHED) {
            if(!usb_probe_slow && ctx->now_ms-usb_probe_started>=8000) {
                usb_probe_slow=true;return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_NONE;
        }
        reader_media_info_t info={0};
        if(state==ESP_OK)state=reader_media_get_info(&info);
        ttf_font_suspend_sd(false);
        const char *saved=app_settings_font_path();
        if(state==ESP_OK && info.present && info.mounted && saved &&
           !strncmp(saved,"/sdcard/",8) && strcmp(saved,ttf_font_path())) (void)ttf_font_open(saved);
        ctx->request_app=&app_reader;
        return APP_REDRAW_NONE;
    }
    if(usb_pending) {
        usb_pending=false;
        usb_error=reader_usb_start();
        usb_snapshot=reader_usb_state();
        if(usb_error!=ESP_OK) {usb_snapshot=READER_USB_ERROR;usb_restart_needed=reader_usb_requires_restart();}
        return APP_REDRAW_PAGE;
    }
    reader_usb_state_t state=reader_usb_state();
    if(state!=usb_snapshot && usb_error==ESP_OK) {usb_snapshot=state;return APP_REDRAW_PAGE;}
    return APP_REDRAW_NONE;
}
const app_desc_t app_usb_transfer={
    .title="USB 传书",.detail="SD 卡文件传输",
    .owns_keys=true,.no_menu=true,
    .blocks_idle_sleep=true,.blocks_media_access=true,.blocks_power_lock=true,
    .on_enter=usb_enter,.on_exit=usb_leave,.render=usb_render,.present=usb_present,
    .on_gesture=usb_gesture,.on_key=usb_key,.on_tick=usb_tick,
};
