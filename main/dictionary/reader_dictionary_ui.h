// SPDX-License-Identifier: Apache-2.0
// 原阅读页上的选词覆盖层与释义卡。/ Selection overlay and definition card on the original reading page.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "epdiy.h"

/// 同时取得原页字形坐标和帧快照，失败不进入选词。/ Capture original glyph positions and framebuffer atomically; failure leaves selection closed.
bool reader_dict_ui_open(const char *text, size_t len, size_t page, EpdRect body,
                         int px, uint8_t *fb, int footer_top);
/// 在原字形上长按松手，直接查询该字或完整英文词。/ Look up the character or whole English word at the original held glyph.
bool reader_dict_ui_open_at(const char *text, size_t len, size_t page, EpdRect body,
                            int px, uint8_t *fb, int footer_top, uint16_t x, uint16_t y);
/// 恢复原帧并释放所有临时内存。/ Restore the captured frame and release all temporary storage.
void reader_dict_ui_close(void);
bool reader_dict_ui_active(void);
bool reader_dict_ui_selecting(void);
bool reader_dict_ui_tap(uint16_t x, uint16_t y);
/// 卡片外正文长按松手后替换释义，保留原页快照；未命中不改变当前卡。/ Replace the card on a body hold-release, preserving the snapshot and leaving misses unchanged.
bool reader_dict_ui_query_at(uint16_t x, uint16_t y);
/// 只从快照绘制覆盖层，不重排或重画正文。/ Draw overlays from the snapshot without relayout or body redraw.
void reader_dict_ui_draw(uint8_t *fb);

/// 设置后续显式查询的来源书名。/ Set source title for subsequent explicit queries.
void reader_dict_ui_set_book(const char *title);
/// 单词本只读回看，关闭时还原列表，不增加查询次数。/ Read-only notebook review, restoring the list on close without incrementing counts.
bool reader_dict_ui_review(const char *query,uint8_t *fb,int footer_top);

/// 长按到阈值按局部空间评分比较整词或单字，只描最佳目标边框，不查询或记录。/ At hold threshold, compare words or characters with local spatial scores and frame only the best target without lookup or recording.
/// 精确字形优先；分数不是概率，词卡外复用原快照。/ Exact glyph hits lead; scores are not probabilities, and an existing card reuses its snapshot.
bool reader_dict_ui_hold_at(const char *text,size_t len,size_t page,EpdRect body,int px,uint8_t *fb,int footer_top,uint16_t x,uint16_t y);
bool reader_dict_ui_hold_existing(uint16_t x,uint16_t y);
bool reader_dict_ui_holding(void);
/// 有效松手只查询一次；取消还原原页或先前词卡。/ A valid release queries once; cancellation restores the original page or previous card.
bool reader_dict_ui_hold_finish(void);
bool reader_dict_ui_hold_cancel(void);
/// 本次目标变化前后两框的脏区，用于局部反馈刷新。/ Dirty area of the old and new targets for this feedback update.
EpdRect reader_dict_ui_hold_area(void);

/// 首段纵移避让手指并保留初始行，稳定后以相对偏移上下换本页可见行；左右调词。
/// Initial vertical escape retains the original row; after settling, relative movement selects visible rows on this page, while horizontal movement retargets words.
bool reader_dict_ui_hold_move(uint16_t x,uint16_t y);
/// 每轮保持时（含静止）传入单调毫秒；只更新避让锚点，不绘制、不查询、不读字体。
/// On every held tick (including stationary contact), pass monotonic milliseconds; update the escape anchor only, without drawing, lookup or font reads.
void reader_dict_ui_hold_tick(int64_t now_ms);

/// 取消前取得整条拖动轨迹，用于灰阶恢复且不可在清状态后再取。/ Capture the entire drag trail before clearing state for grayscale restoration.
EpdRect reader_dict_ui_hold_restore_area(void);
