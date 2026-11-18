/*
 * SPDX-License-Identifier: Apache-2.0
 * 设备无关的阅读按键与前光策略。/ Hardware-independent reader key and frontlight policy.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    READER_KEY_NONE = 0,
    READER_KEY_SETTINGS,
    READER_KEY_BACK,
    READER_KEY_REFRESH,
    READER_KEY_HOME,
    READER_KEY_PREVIOUS,
    READER_KEY_NEXT,
    READER_KEY_SCREENSHOT,
} reader_key_action_t;

typedef struct {
    bool initialized; ///< 首次采样已完成 / First sample received
    bool interrupted; ///< 无效采样打断确认 / Invalid sample interrupted confirmation
    uint8_t stable, candidate, armed; ///< 稳态、候选及可触发位 / Stable, candidate and armed bits
    int64_t since_ms, last_ms; ///< 候选起点和上次采样 / Candidate start and last sample
} reader_key_state_t;

/// 按正面物理位置返回动作，越界不触发。/ Map front physical position to action; reject out-of-range positions.
reader_key_action_t reader_controls_front_key(bool metalio, int position);
/// 位0电源、位1上页、位2下页、位3截图；30ms确认，每次按下只返回一次。
/// Bits 0 power, 1 previous, 2 next, 3 screenshot; 30ms confirmation, one event per press.
uint8_t reader_controls_side_update(reader_key_state_t* state, bool valid, uint8_t pressed, int64_t now_ms);
/// 输出千分比占空比；关灯、睡眠或非法设置输出0。/ Return duty in permille; off, sleep or invalid settings yield zero.
void reader_controls_light(uint8_t level, uint8_t warmth, bool asleep, uint16_t* cool, uint16_t* warm);

/// 侧键独立于正面三键编号。/ Side-key codes are separate from the front three keys.
enum { READER_INPUT_PREVIOUS = 16, READER_INPUT_NEXT, READER_INPUT_SCREENSHOT };

/// 触摸面板坐标与屏外三键归一化，无效点必须取消手势。/ Normalize panel coordinates and off-screen keys; invalid points must cancel gestures.
bool reader_controls_touch_xy(uint16_t raw_x, uint16_t raw_y, uint8_t count, uint16_t* x, uint16_t* y);
