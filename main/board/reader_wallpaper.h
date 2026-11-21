/* SPDX-License-Identifier: Apache-2.0
 * 小纸壁纸预读：只在空闲读卡，显示阶段只借用完整内存图。/ Pico wallpaper preload: media work only at idle; display borrows complete memory images.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define READER_WALLPAPER_BYTES (684u*1216u/2u)
#define READER_WALLPAPER_NAME_BYTES 96
/// 每次最多一个目录项或 8KiB；有封面的锁屏不预读。/ At most one directory entry or 8KiB per step; skip lock preload when a cover exists.
void reader_wallpaper_idle(bool off,bool cover);
/// 关闭所有未完成句柄并释放缓存；USB/失卡先调用。/ Close pending handles and free cache before USB ownership or media loss.
void reader_wallpaper_invalidate(void);
/// 必须在屏幕上电前调用；未完整准备则返回 NULL，不同步读文件。/ Call before display power-up; return NULL if incomplete, never synchronously load a file.
const uint8_t *reader_wallpaper_acquire(bool off,bool cover);
/// 显示成功才提交轮换位置；随后释放借用图。/ Commit rotation only after successful display, then release the borrowed image.
void reader_wallpaper_finish(bool displayed);
