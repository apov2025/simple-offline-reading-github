/* SPDX-License-Identifier: Apache-2.0
 * 阅读器触摸快照，Pico 保留原类型 ABI。/ Reader touch snapshots; Pico retains its original type ABI.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifndef READER_METALIO
#define READER_METALIO 0
#endif
#define READER_TOUCH_MAX_POINTS 2
// 产品页面共用的三键编号，不依赖演示菜单。/ Shared product key IDs, independent of demo menus.
#define UI_KEY_1 0
#define UI_KEY_2 1
#define UI_KEY_3 2
#if READER_METALIO
#include "esp_err.h"
typedef void* reader_touch_handle_t;
typedef struct { bool active; uint8_t id, event; uint16_t x, y; } reader_touch_point_t;
typedef struct {
    bool touched;
    uint8_t count;
    uint16_t x, y;
    reader_touch_point_t points[2];
    uint8_t raw[15];
} reader_touch_t;
typedef struct { uint8_t fw, id, module, project; uint16_t type; } reader_touch_info_t;
esp_err_t reader_touch_read(reader_touch_handle_t handle, reader_touch_t* touch);
esp_err_t reader_touch_get_info(reader_touch_handle_t handle, reader_touch_info_t* info);
#else
#include "cst836u.h"
typedef cst836u_handle_t reader_touch_handle_t;
typedef cst836u_touch_t reader_touch_t;
typedef cst836u_info_t reader_touch_info_t;
#define reader_touch_read cst836u_read
#define reader_touch_get_info cst836u_get_info
#endif
