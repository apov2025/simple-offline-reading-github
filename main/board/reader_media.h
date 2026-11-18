/* SPDX-License-Identifier: Apache-2.0
 * 阅读介质契约：失效时先关闭消费者再显式重挂载。/ Reader media contract: close consumers before explicit remount.
 */
#pragma once
#if READER_METALIO
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool present, mounted, needs_format;
    char name[8];
    uint64_t capacity_bytes, free_bytes;
    esp_err_t error;
} reader_media_info_t;
esp_err_t reader_media_start_probe(void);
esp_err_t reader_media_get_info(reader_media_info_t* info);
esp_err_t reader_media_remount(void);
esp_err_t reader_media_sync(void);
#else
#include "read_pico_sd.h"
typedef read_pico_sd_info_t reader_media_info_t;
#define reader_media_start_probe read_pico_sd_start_probe
#define reader_media_get_info read_pico_sd_get_info
#define reader_media_remount read_pico_sd_remount
#define reader_media_sync read_pico_sd_sync
#endif
