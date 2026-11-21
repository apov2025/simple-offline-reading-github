/* SPDX-License-Identifier: Apache-2.0
 * 设备信息与 USB 开关共用排版，纯绘制，不访问介质。/ Shared device information and USB switch layout; paint only, no media access.
 */
#include "ui_device_panel.h"
#include "ui_kit.h"
#include "esp_app_desc.h"
#include <stdio.h>
#include <string.h>

bool ui_device_usb_hit(int x, int y, bool show_manual) {
    if(show_manual)y-=UI_DEVICE_MANUAL_OFFSET;
    return x>=ui_content_right()-144 && x<ui_content_right() && y>=UI_DEVICE_USB_TOP && y<UI_DEVICE_USB_BOTTOM;
}
bool ui_device_manual_hit(int x,int y) {
    return x>=UI_MARGIN && x<ui_content_right() && y>=UI_DEVICE_MANUAL_TOP && y<UI_DEVICE_MANUAL_BOTTOM;
}
void ui_device_panel_draw(uint8_t* fb,const ui_battery_t* battery,bool metalio,ui_device_usb_state_t usb_state,bool show_manual) {
    if(show_manual && !metalio){ui_device_reader_panel_draw(fb,battery,"","","",false);return;}
    show_manual=show_manual && !metalio;
    int offset=show_manual?UI_DEVICE_MANUAL_OFFSET:0;
    int height=metalio?248:UI_DEVICE_PANEL_HEIGHT+offset;
    epd_fill_rect((EpdRect){0,0,UI_LOCK_WIDTH,height},UI_GRAY_WHITE,fb);
    ui_text(fb,UI_MARGIN,38,30,metalio?"设备：无名 Metalio":"设备：小纸 Pico by 思维重置",EPD_DRAW_ALIGN_LEFT,false);
    ui_text(fb,UI_MARGIN,100,30,"固件名称：离线阅读 by apov",EPD_DRAW_ALIGN_LEFT,false);
    // Keep the exact build identifier in AppDesc/logs; simplify only the visible label.
    char shown[32];snprintf(shown,sizeof(shown),"%.31s",esp_app_get_description()->version);
    char* preview=strstr(shown,"-preview.");
    if(preview)preview[8]='\0';
    char version[80];snprintf(version,sizeof(version),"固件版本：%s",shown);
    ui_text(fb,UI_MARGIN,158,26,version,EPD_DRAW_ALIGN_LEFT,false);
    ui_battery_draw(fb,ui_content_right()-UI_BATTERY_WIDTH,104,battery);
    if(!metalio) {
        ui_hairline(fb,216,UI_MARGIN,ui_content_width(),UI_GRAY_LIGHT);
        if(show_manual) {
            ui_text_vc(fb,UI_MARGIN,268,36,"使用说明",EPD_DRAW_ALIGN_LEFT,false);
            int x=ui_content_right()-16;
            for(int stroke=0;stroke<2;stroke++) {
                epd_draw_line(x-5+stroke,259,x+4+stroke,268,UI_GRAY_BLACK,fb);
                epd_draw_line(x+4+stroke,268,x-5+stroke,277,UI_GRAY_BLACK,fb);
            }
            ui_hairline(fb,312,UI_MARGIN,ui_content_width(),UI_GRAY_LIGHT);
        }
        ui_text_vc(fb,UI_MARGIN,280+offset,36,"USB传输",EPD_DRAW_ALIGN_LEFT,false);
        if(usb_state==UI_DEVICE_USB_STARTING || usb_state==UI_DEVICE_USB_ENDING) {
            ui_text_vc(fb,ui_content_right(),280+offset,28,
                usb_state==UI_DEVICE_USB_STARTING?"正在开启":"正在结束",EPD_DRAW_ALIGN_RIGHT,false);
        } else {
            if(usb_state==UI_DEVICE_USB_ON)
                ui_text_vc(fb,ui_content_right()-154,280+offset,28,"已开启",EPD_DRAW_ALIGN_RIGHT,false);
            EpdRect button={ui_content_right()-128,246+offset,128,68};
            ui_draw_round_rect(fb,button,10,UI_GRAY_BLACK);
            ui_text_vc(fb,button.x+button.width/2,280+offset,30,
                usb_state==UI_DEVICE_USB_ON?"结束":"开启",EPD_DRAW_ALIGN_CENTER,false);
        }
    }
    ui_hairline(fb,height-1,0,UI_LOCK_WIDTH,UI_GRAY_LIGHT);
    ui_fill_round_rect(fb,(EpdRect){UI_LOCK_WIDTH/2-30,height-19,60,4},2,UI_GRAY_LIGHT);
}

bool ui_device_wifi_hit(int x,int y) {
    return x>=ui_content_right()-144 && x<ui_content_right() && y>=460 && y<540;
}
bool ui_device_clock_hit(int x,int y) {
    return x>=UI_MARGIN && x<UI_MARGIN+212 && y>=550 && y<626;
}
static void reader_action(uint8_t *fb,int y,const char *label,const char *action) {
    ui_text_vc(fb,UI_MARGIN,y,32,label,EPD_DRAW_ALIGN_LEFT,false);
    EpdRect button={ui_content_right()-128,y-34,128,68};
    ui_draw_round_rect(fb,button,10,UI_GRAY_BLACK);
    ui_text_vc(fb,button.x+button.width/2,y,28,action,EPD_DRAW_ALIGN_CENTER,false);
}
void ui_device_reader_panel_draw(uint8_t *fb,const ui_battery_t *battery,const char *date,const char *clock,const char *feedback,bool syncing) {
    epd_fill_rect((EpdRect){0,0,UI_LOCK_WIDTH,UI_DEVICE_READER_PANEL_HEIGHT},UI_GRAY_WHITE,fb);
    ui_text(fb,UI_MARGIN,34,28,"设备：小纸 Pico by 思维重置",EPD_DRAW_ALIGN_LEFT,false);
    ui_text(fb,UI_MARGIN,86,28,"固件名称：离线阅读 by apov",EPD_DRAW_ALIGN_LEFT,false);
    char shown[32];snprintf(shown,sizeof(shown),"%.31s",esp_app_get_description()->version);
    char *preview=strstr(shown,"-preview.");if(preview)preview[8]='\0';
    char version[80];snprintf(version,sizeof(version),"固件版本：%s",shown);
    ui_text(fb,UI_MARGIN,138,26,version,EPD_DRAW_ALIGN_LEFT,false);
    ui_text(fb,UI_MARGIN,210,30,date && *date?date:"时间未校准",EPD_DRAW_ALIGN_LEFT,false);
    ui_text(fb,ui_content_right(),210,32,clock?clock:"",EPD_DRAW_ALIGN_RIGHT,false);
    ui_battery_draw(fb,ui_content_right()-UI_BATTERY_WIDTH,252,battery);
    ui_hairline(fb,284,UI_MARGIN,ui_content_width(),UI_GRAY_LIGHT);
    ui_text_vc(fb,UI_MARGIN,324,32,"使用说明",EPD_DRAW_ALIGN_LEFT,false);
    int x=ui_content_right()-16;
    epd_draw_line(x-5,315,x+4,324,UI_GRAY_BLACK,fb);
    epd_draw_line(x+4,324,x-5,333,UI_GRAY_BLACK,fb);
    ui_hairline(fb,364,UI_MARGIN,ui_content_width(),UI_GRAY_LIGHT);
    reader_action(fb,412,"USB传输","开启");
    ui_hairline(fb,456,UI_MARGIN,ui_content_width(),UI_GRAY_LIGHT);
    reader_action(fb,500,"Wi-Fi传书","开启");
    ui_hairline(fb,544,UI_MARGIN,ui_content_width(),UI_GRAY_LIGHT);
    EpdRect button={UI_MARGIN,554,204,68};
    ui_draw_round_rect(fb,button,10,syncing?UI_GRAY_LIGHT:UI_GRAY_BLACK);
    ui_text_vc(fb,button.x+button.width/2,588,28,"校准时间",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_vc(fb,ui_content_right(),588,26,feedback?feedback:"",EPD_DRAW_ALIGN_RIGHT,false);
    ui_hairline(fb,UI_DEVICE_READER_PANEL_HEIGHT-1,0,UI_LOCK_WIDTH,UI_GRAY_LIGHT);
    ui_fill_round_rect(fb,(EpdRect){UI_LOCK_WIDTH/2-30,UI_DEVICE_READER_PANEL_HEIGHT-19,60,4},2,UI_GRAY_LIGHT);
}
