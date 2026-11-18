/* SPDX-License-Identifier: Apache-2.0
 * 保存纵向阅读截图，不管理挂载。/ Save portrait screenshots without managing mounts.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/// 原生横屏4bpp低半字节优先，转换为反向纵屏BMP；独占新文件，失败清理。
/// Native landscape low-nibble-first 4bpp to inverted portrait BMP; exclusive new file, cleanup on failure.
bool reader_screenshot_write(const char* directory, const char* stem, const uint8_t* fb,
    int width, int height, char* path, size_t capacity);
