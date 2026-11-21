/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：独立 EPUB 书源，按 OPF spine 顺序提供章节与文本块。
 * English: Independent EPUB source exposing chapters and text blocks in OPF spine order.
 *
 * 冻结：进度使用 spine 原始 HTML 未压缩字节的累计值，不是 ZIP 物理偏移；不接入主循环。
 * Frozen: Progress uses cumulative uncompressed source HTML bytes, not physical ZIP offsets; no event-loop integration.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "html_text.h"

typedef struct book_epub book_epub_t;
#define BOOK_TOC_TITLE_CAP 160
/// 独立导航条目；chapter 始终指向原 spine，不重排续读位置。/ Independent navigation entry; chapter keeps the original spine index.
typedef struct {
    char title[BOOK_TOC_TITLE_CAP]; ///< UTF-8 标签 / UTF-8 label
    unsigned depth; ///< 零基层级 / Zero-based nesting depth
    size_t chapter; ///< 原 spine 章节 / Original spine chapter
} book_toc_entry_t;
/// 返回导航数量；NCX 优先，缺失或损坏时尝试 NAV，最终使用 spine。
/// Navigation count; prefer NCX, try NAV when absent/broken, then fall back to spine.
/// 目录最多 32768 项、64 层 XML 与 1 MiB 标签/片段，超限回退。/ Bound navigation to 32768 entries, 64 XML levels and 1 MiB of labels/fragments; overflow falls back.
size_t book_epub_toc_count(const book_epub_t *book);
/// 读取目录项，不加载正文。/ Read a navigation entry without loading body text.
esp_err_t book_epub_toc_get(const book_epub_t *book, size_t index, book_toc_entry_t *out);
/// 按需解析锚点为正文 UTF-8 字节位置；失败不改输出或阅读状态。/ Resolve anchors on demand to body UTF-8 offsets; failure preserves outputs and reading state.
esp_err_t book_epub_toc_resolve(book_epub_t *book, size_t index, size_t *chapter, size_t *offset);

/// 查当前位置之前最近的有效目录；没有前项时取首个后项，缺失片段不参与选择。
/// Find the nearest valid preceding TOC location, otherwise the first upcoming one; ignore missing fragments.
/// 同位置优先深层，再取最早行；每章片段单遍缓存，失败保留 index 和阅读状态。
/// Ties prefer greater depth then earliest row; cache fragments in one pass per chapter, preserving index and reading state on failure.
esp_err_t book_epub_toc_current(book_epub_t *book, size_t chapter, size_t offset, size_t *index);

/// 书库列表用的书目信息，取自 OPF 的 dc:title / dc:creator / dc:date；没有的项是空串。
/// Listing metadata from the OPF dc:title / dc:creator / dc:date; absent fields are empty strings.
typedef struct {
    char title[160];  ///< 书名，过长截断 / Title, truncated when too long
    char author[96];  ///< 第一位作者 / First creator
    char year[8];     ///< dc:date 开头的四位年份 / Leading four-digit year of dc:date
} book_epub_meta_t;
/// 只读书目信息，不建章节表，比 open 快得多；文件不是可读的 EPUB 时返回错误且 out 清空。
/// Read listing metadata only, without building the chapter table; much cheaper than open. Clears out and fails when the file is not a readable EPUB.
esp_err_t book_epub_read_meta(const char *path, book_epub_meta_t *out);
/// 复用已打开的 ZIP 目录读取书目，不重开文件；失败清空输出。/ Reuse the open ZIP index for metadata; clear outputs on failure.
esp_err_t book_epub_read_open_meta(book_epub_t *book, book_epub_meta_t *out);
/// 从已打开的书读取定长条目；输出由调用方释放，失败为 NULL。/ Read an exact-size entry from an open book; caller frees output, NULL on failure.
esp_err_t book_epub_read_open_file(book_epub_t *book, const char *name, size_t len, uint8_t **out);

/// 整理程序放在书里的封面图的路径和大小：整屏 4 位灰度，684×1216，每字节两个像素。
/// Path and size of the cover image the preparation tool stores in a book: full-screen 4-bit gray, 684×1216, two pixels per byte.
#define BOOK_EPUB_COVER_PATH "pico/cover.4bpp"
#define BOOK_EPUB_COVER_BYTES (684u * 1216u / 2u)
/// 读出书里一个大小正好是 len 的文件（不经过章节表）；没有、大小不符或读失败时 out 置空。成功后调用方 free。
/// Read one file of exactly len bytes from a book, bypassing the chapter table; out is NULL when it is absent, differs in size or fails to read. The caller frees it on success.
esp_err_t book_epub_read_file(const char *path, const char *name, size_t len, uint8_t **out);
/// 打开 EPUB；失败时 out 置空，成功后由 close 释放。/ Open EPUB; clear out on failure, close owns successful cleanup.
esp_err_t book_epub_open(const char *path, book_epub_t **out);
/// 关闭 ZIP 并释放目录，可传 NULL。/ Close ZIP and free the index; NULL is allowed.
void book_epub_close(book_epub_t *book);
/// 返回 spine 章节数，上限 32768。/ Return the spine chapter count, at most 32768.
size_t book_epub_chapter_count(const book_epub_t *book);
/// 复制 UTF-8 标题，容量不足报错而不截断。/ Copy a UTF-8 title; report insufficient capacity instead of truncating.
esp_err_t book_epub_chapter_title(const book_epub_t *book, size_t index, char *buf, size_t cap);
/// 加载 UTF-8、标题/段落及图片引用，不解码图片；调用方 html_text_free。/ Load UTF-8, styled blocks and image references without decoding images; caller uses html_text_free.
esp_err_t book_epub_load(book_epub_t *book, size_t index, html_text_t *out);
/// 明确请求后解码本章单幅本地图片；失败输出清空，成功像素由调用方 free，不修改正文或分页。
/// Decode one local image on explicit request; clear outputs on failure, caller frees successful pixels; text and pagination stay unchanged.
esp_err_t book_epub_load_image(book_epub_t *book, size_t index, const char *reference,
                              uint8_t **pixels, uint16_t *width, uint16_t *height);
/// spine 原始 HTML 未压缩字节总量，不等于 EPUB 文件大小。/ Total uncompressed source HTML bytes in the spine, not EPUB file size.
uint32_t book_epub_total_bytes(const book_epub_t *book);
/// 该章之前所有 spine 原始 HTML 字节累计值。/ Sum of source HTML bytes preceding this chapter in the spine.
uint32_t book_epub_chapter_byte_offset(const book_epub_t *book, size_t index);
