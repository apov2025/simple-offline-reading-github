/* SPDX-License-Identifier: Apache-2.0
 * 中文：纯本地、有限规则的 UTF-8 句界解析，不读取文件或字体。
 * English: Local UTF-8 sentence spans using bounded rules, without file or font access.
 * 冻结：只返回原文字节范围；不改写文本、不分配内存、不使用网络。
 * Frozen: Return original byte spans only; no rewriting, allocation or network.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/// 返回 at 所在句与可见页交集的半开字节范围，并去首尾空白；at 必须是可见字符起点。
/// Return the sentence containing at, clipped to the page and trimmed, as a half-open byte span; at must start a visible character.
/// 传入 0,len 可恢复章内完整句子，不依赖分页；不能越过传入文本的章节边界。
/// Pass 0,len for the complete chapter-local sentence regardless of page layout; spans cannot cross the supplied chapter text.
/// UTF-8 或边界无效、空结果时失败且不改输出；缩写仅做有限规则识别。
/// Invalid UTF-8/boundaries or an empty result fail without changing outputs; abbreviation handling is deliberately limited.
/// 常见缩写、姓名首字母、小数不作句界；连续省略号、终止标点后的紧邻闭引号/括号归前句。
/// Common abbreviations, name initials and decimals suppress period boundaries; ellipses and adjacent closing quotes/brackets stay with the preceding sentence.
bool reader_sentence_span(const char *text, size_t len, size_t page_start,
                          size_t page_end, size_t at, size_t *start, size_t *end);

/// 验证片段为有效 UTF-8 且全部属于句界裁剪使用的同一空白集合；空片段（含 NULL）为 true。
/// Check valid UTF-8 containing only the sentence trimmer's whitespace set; empty spans (including NULL) are true.
bool reader_sentence_whitespace(const char *text, size_t len);
