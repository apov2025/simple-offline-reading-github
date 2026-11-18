/* SPDX-License-Identifier: Apache-2.0
 * Metalio 内部板级生命周期。/ Metalio internal board lifecycle.
 */
#pragma once
#include "reader_board.h"
#include "esp_err.h"
esp_err_t metalio_reader_init(void);
esp_err_t metalio_reader_power_level(bool* down);
void metalio_reader_clear_keys(void);
void metalio_reader_light_sleep(bool asleep);

// S31 SDK 兼容层不改已安装 SDK。/ S31 SDK compatibility leaves the installed SDK untouched.
void metalio_reader_sleep_init(void);
esp_err_t metalio_reader_light_wait(void);
