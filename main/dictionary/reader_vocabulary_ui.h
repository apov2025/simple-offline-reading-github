/* SPDX-License-Identifier: Apache-2.0
 * 单词本列表，仅由隐藏按键入口打开。/ Notebook list, opened only through its hidden key gesture.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
void reader_vocabulary_ui_open(void);
void reader_vocabulary_ui_close(void);
bool reader_vocabulary_ui_active(void);
/// 详情返回原列表页，不读卡；列表状态返回 false。/ Return from details to the cached list page without media reads; false if already on the list.
bool reader_vocabulary_ui_back(void);
/// 纯绘制已缓存的释义，当前词库只在开本或翻页时读取。/ Draw cached meanings only; current dictionary lookup happens on opening or page changes.
void reader_vocabulary_ui_draw(uint8_t *fb);
bool reader_vocabulary_ui_tap(uint16_t x,uint16_t y,uint8_t *fb);
bool reader_vocabulary_ui_move(int direction);
