/* SPDX-License-Identifier: Apache-2.0
 * 电量显示状态，不把无效读数当作零。/ Battery presentation state; invalid is not zero.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool valid, charging, low; uint8_t percent; } ui_battery_t;
static inline void ui_battery_update(ui_battery_t* b, bool valid, unsigned permille, bool charging) {
    b->valid=valid && permille<=1000;
    if(!b->valid) return;
    b->percent=(uint8_t)(permille/10);
    b->charging=charging;
    if(permille<=200) b->low=true;
    else if(permille>=250) b->low=false;
}
static inline bool ui_battery_low(const ui_battery_t* b) { return b->valid && b->low; }
#define UI_BATTERY_WIDTH 80
#define UI_BATTERY_GRAY 0x80
/// 绘制缓存电量，不读取硬件。/ Draw cached charge without hardware reads.
void ui_battery_draw(uint8_t* fb, int x, int y, const ui_battery_t* battery);
