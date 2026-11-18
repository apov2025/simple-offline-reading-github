/* SPDX-License-Identifier: Apache-2.0
 * 锁屏不叠字；断电前绘制关机留屏，失败时必须替换。
 * Keep lock images unobstructed; draw the retained off screen before power loss and replace it on failure.
 */
#pragma once
#include <stdint.h>
typedef enum { UI_POWER_LOCKED, UI_POWER_SHUTTING_DOWN, UI_POWER_FAILED } ui_power_screen_t;
void ui_draw_power_screen(uint8_t* fb,ui_power_screen_t state,const uint8_t* cover);
