/* SPDX-License-Identifier: Apache-2.0
 * 小纸原页划线：显示缓存与临时选区，磁盘仅在装章/确认时使用。
 * Pico original-page underlining: cached marks and a draft; disk only at chapter load/confirmation.
 */
#pragma once
#include "epdiy.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void reader_highlight_ui_chapter(const char *path,const char *title,uint32_t bytes,size_t chapter,const char *chapter_title,const char *text,size_t len);
void reader_highlight_ui_reset(void);
void reader_highlight_ui_close(void);
bool reader_highlight_ui_active(void);
bool reader_highlight_ui_open(size_t page,EpdRect body,int px,uint8_t *fb,int footer);
/// 原页点选完整句，相邻句扩选；范围使用章内原文字节，不依赖分页或字号。
/// Select full sentences on this page and extend to neighbors; ranges use chapter source bytes independently of pagination/font size.
bool reader_highlight_ui_tap(uint16_t x,uint16_t y);
void reader_highlight_ui_draw(uint8_t *fb);
void reader_highlight_ui_marks(uint8_t *fb,size_t page,EpdRect body,int px);
