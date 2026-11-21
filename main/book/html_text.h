/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：把章节 HTML 转为 PSRAM UTF-8 文本与非空块表。
 * English: Convert chapter HTML into PSRAM UTF-8 text and nonempty blocks.
 *
 * 冻结：不加载资源、不执行脚本、不解释 CSS；输入只借用。
 * Frozen: Never load resources, execute scripts or interpret CSS; input is borrowed.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define HTML_TEXT_MAX_BYTES (2u * 1024u * 1024u)
#define HTML_TEXT_MAX_BLOCKS 16384u

typedef struct {
    size_t offset; ///< UTF-8 字节起点 / UTF-8 byte start
    size_t len; ///< 不含段间换行的字节数 / Bytes excluding the block separator
    bool heading; ///< h1–h3 标题块 / h1-h3 heading block
    char* image_src; ///< 图片引用，块表持有；缺失时保持文字占位 / Owned image reference; absent references retain text placeholders
    uint8_t* image; ///< 模块持有的八位灰度图，NULL 时绘制占位 / Owned 8-bit grayscale pixels; NULL renders the placeholder
    uint16_t image_width, image_height; ///< 已缩放图像尺寸 / Resized image dimensions
    uint16_t image_first_chapter; ///< 已读章节中同路径图片最早的零基序号 / Earliest zero-based visited chapter containing this image path
    bool image_title; ///< 章首标题前的图片或同资源引用，独立提示 / Image before an opening heading, or the same resource elsewhere, shown separately
    bool image_repeated; ///< 先前已读章节出现过同路径图片 / The same image path appeared in an earlier visited chapter
} blk_t;

typedef struct {
    char* utf8; ///< 模块分配且零结尾的文本 / Owned NUL-terminated text
    size_t len; ///< 不含结尾零的字节数 / Bytes excluding terminating NUL
    blk_t* blocks; ///< 模块分配的非空块表 / Owned nonempty block table
    size_t count; ///< 块数 / Block count
} html_text_t;

/// 输出须为空；成功交出所有权，失败清空输出；空输入成功且零块。/ Output must be empty; success transfers ownership, failure clears output; empty input succeeds with zero blocks.
esp_err_t html_to_blocks(const char* html, size_t len, html_text_t* out);
/// 释放文本与块表并清零；可重复调用。/ Free text and blocks and reset; safe to repeat.
void html_text_free(html_text_t* text);
/// 释放块表及其图片，不释放正文；可传 NULL。/ Free blocks and their images without freeing text; NULL is allowed.
void html_blocks_free(blk_t* blocks, size_t count);

/// 使用相同文本转换规则定位 id 或旧式 a name；失败不改 offset，无资源访问。/ Resolve id or legacy a name using the same text conversion; failure preserves offset, with no resource access.
esp_err_t html_anchor_offset(const char *html, size_t len, const char *anchor, size_t *offset);

/// 一次转换的锚点查询；name 借用，成功时缺失的 offset 为 SIZE_MAX。/ Batched anchor query; name is borrowed, missing offsets become SIZE_MAX on success.
typedef struct {
    const char *name; ///< 区分大小写的 id 或 a name / Case-sensitive id or a name
    size_t offset; ///< 转换后的 UTF-8 字节位置 / Converted UTF-8 byte position
} html_anchor_t;
/// 单遍转换解析至多 32768 个锚点；失败保留所有输出，无资源访问。/ Resolve at most 32768 anchors in one conversion; preserve outputs on failure, with no resource access.
esp_err_t html_anchor_offsets(const char *html, size_t len, html_anchor_t *anchors, size_t count);
