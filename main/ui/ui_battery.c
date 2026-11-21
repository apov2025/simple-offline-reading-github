/* SPDX-License-Identifier: Apache-2.0
 * 细长电池仅用填充表示电量，不显示数字。/ Slim battery shows charge by fill only, without numeric labels.
 */
#include "ui_battery.h"
#include "ui_kit.h"
#include "ttf_font.h"
void ui_battery_draw(uint8_t* fb,int x,int y,const ui_battery_t* b) {
    const int icon_x=x+24;
    epd_fill_rect((EpdRect){icon_x,y+7,52,18},UI_BATTERY_GRAY,fb);
    epd_fill_rect((EpdRect){icon_x+2,y+9,48,14},UI_GRAY_WHITE,fb);
    epd_fill_rect((EpdRect){icon_x+52,y+12,4,8},UI_BATTERY_GRAY,fb);
    if(b->valid) {
        int filled=(44*b->percent+99)/100;
        if(filled)epd_fill_rect((EpdRect){icon_x+4,y+11,filled,10},UI_BATTERY_GRAY,fb);
    }
    // 外侧符号仅标记充电或读数未知，不覆盖电量条。/ Outside marks indicate charging or unavailable telemetry without touching the fill.
    if(!b->valid) {
        int above=0,below=0;ttf_measure_line_px(24,"?",&above,&below);
        ttf_draw_text_px(fb,x,y+16+(above-below)/2,24,"?",EPD_DRAW_ALIGN_LEFT,UI_BATTERY_GRAY>>4,UI_INK_WHITE);
    }
    else if(b->charging) {
        epd_fill_triangle(x+14,y+4,x+2,y+18,x+10,y+18,UI_BATTERY_GRAY,fb);
        epd_fill_triangle(x+7,y+14,x+17,y+14,x+5,y+28,UI_BATTERY_GRAY,fb);
    }
}
