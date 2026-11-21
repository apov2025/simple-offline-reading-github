/* SPDX-License-Identifier: Apache-2.0
 * 紧凑设备下拉面板。/ Compact device pull-down panel.
 */
#pragma once
#include "ui_battery.h"
#define UI_DEVICE_PANEL_HEIGHT 372
#define UI_DEVICE_MANUAL_OFFSET 132
#define UI_DEVICE_READER_PANEL_HEIGHT 660
#define UI_DEVICE_MANUAL_TOP 284
#define UI_DEVICE_MANUAL_BOTTOM 364
#define UI_DEVICE_USB_TOP 232
#define UI_DEVICE_USB_BOTTOM 328
typedef enum {
    UI_DEVICE_USB_OFF, UI_DEVICE_USB_ON, UI_DEVICE_USB_STARTING, UI_DEVICE_USB_ENDING
} ui_device_usb_state_t;
void ui_device_panel_draw(uint8_t* fb, const ui_battery_t* battery, bool metalio, ui_device_usb_state_t usb_state, bool show_manual);
bool ui_device_usb_hit(int x, int y, bool show_manual);
/// 仅阅读器下拉面板启用的整行说明书入口。/ Whole-row manual target, enabled only in the reader drawer.
bool ui_device_manual_hit(int x, int y);

/// 小纸阅读器专用信息面板；日期/时间/状态为调用方缓存，仅绘制。/ Pico reader drawer; paint cached date/time/status only.
void ui_device_reader_panel_draw(uint8_t *fb,const ui_battery_t *battery,const char *date,const char *clock,const char *feedback,bool syncing);
bool ui_device_wifi_hit(int x,int y);
bool ui_device_clock_hit(int x,int y);
