/* SPDX-License-Identifier: Apache-2.0
 * 锁屏保留书封；关机使用独立白底电源符号。/ Keep the cover for lock; use a distinct white shutdown screen.
 */
#include "ui_power_screen.h"
#include "ui_kit.h"
#include "../assets/cloudzao_logo_4bpp.h"
void ui_draw_power_screen(uint8_t* fb,ui_power_screen_t state,const uint8_t* cover) {
    const int cx=UI_LOCK_WIDTH/2;
    if(state==UI_POWER_LOCKED) {
        if(cover)ui_draw_full_image(fb,cover);
        else {
            // 无书封时显示本机默认锁屏。/ Use this device's default lock screen without a book cover.
            ui_clear_page(fb);
            const int size=METALIO_LOCK_LOGO_SIZE;
            epd_draw_rotated_image((EpdRect){cx-size/2,(UI_LOCK_HEIGHT-size)/2,size,size},
                metalio_lock_logo_4bpp,fb);
        }
        return;
    }
    ui_clear_page(fb);
    epd_fill_circle(cx,470,68,UI_GRAY_BLACK,fb);
    epd_fill_circle(cx,470,62,UI_GRAY_WHITE,fb);
    ui_clear_rect_fast(fb,(EpdRect){cx-28,390,56,55});
    epd_fill_rect((EpdRect){cx-3,382,7,88},UI_GRAY_BLACK,fb);
    ui_text(fb,cx,584,52,state==UI_POWER_SHUTTING_DOWN?"已关机":"关机未完成",EPD_DRAW_ALIGN_CENTER,false);
    if(state==UI_POWER_FAILED)
        ui_text(fb,cx,668,28,"设备仍在运行",EPD_DRAW_ALIGN_CENTER,false);
}
