/*
 * SPDX-License-Identifier: Apache-2.0
 * 小纸 Pico 的手动 USB 存储。/ Manual USB storage on Read Pico.
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    READER_USB_OFF, READER_USB_WAITING, READER_USB_CONNECTED,
    READER_USB_EJECTED, READER_USB_ERROR
} reader_usb_state_t;

/// 仅在所有 FAT 消费者退出后调用。/ Call from the UI after closing all FAT clients.
esp_err_t reader_usb_start(void);
reader_usb_state_t reader_usb_state(void);
/// USB 是否仍独占卡。/ Whether the USB session still reserves the card.
bool reader_usb_active(void);
/// 部分拆除后须重启；此时已排空并封锁读写。/ A partial teardown has drained and blocked I/O and requires restart.
bool reader_usb_requires_restart(void);
/// 电脑未占用或已弹出时可退出，成功后可重新探测卡。/ Exit before host ownership or after eject; success permits probing.
esp_err_t reader_usb_stop(void);
/// 仅在用户确认电脑已弹出或已停止复制并拔线后调用。/ Require confirmation of host eject or stopped copies and unplug.
esp_err_t reader_usb_stop_confirmed(void);
