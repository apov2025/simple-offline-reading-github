/* SPDX-License-Identifier: Apache-2.0
 * 阅读器板级服务。/ Reader board services.
 */
#pragma once
#include <stdint.h>
#include "reader_input.h"
#if READER_METALIO
/// 立即应用前光档位，限制总功率。/ Apply frontlight choices, bounding total power.
void reader_board_set_light(uint8_t level, uint8_t warmth);
bool reader_board_power_ready(void);
bool reader_board_take_power(void);
/// 取一个已去抖的侧键动作。/ Take one debounced side-key action.
int reader_board_take_side_key(void);
static inline bool reader_board_continuous_init(void) { return false; }
#else
#include "read_pico_pmu.h"
#include "continuous_du.h"
#define reader_board_power_ready read_pico_pmu_ready
#define reader_board_take_power read_pico_pmu_take_key_short
#define reader_board_continuous_init continuous_du_init
#endif
