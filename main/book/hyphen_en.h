/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：英文单词的断词位置（Liang 算法 + TeX 的美式英语规则），只算不画。
 * English: Hyphenation points for English words (Liang's algorithm with the TeX American English patterns); computes only, draws nothing.
 *
 * 冻结：只处理纯 ASCII 字母的单词；断点前至少 2 个字母、后至少 3 个（TeX 的默认值）。
 * Frozen: ASCII-letter words only; at least 2 letters before a break and 3 after (the TeX defaults).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/// 能处理的最长单词；更长的不断。/ Longest word handled; longer ones get no breaks.
#define HYPHEN_EN_WORD_MAX 40

/// 标出 word（len 个 ASCII 字母，大小写均可）里可以断开的位置：breaks[i] 非零表示可以在第 i 个字母之前断。
/// breaks 至少 HYPHEN_EN_WORD_MAX 个元素。返回断点个数；含非字母或太长、太短时返回 0，此时不要读 breaks。
/// Mark where word (len ASCII letters, any case) may break: breaks[i] nonzero allows a break before letter i.
/// breaks holds at least HYPHEN_EN_WORD_MAX entries. Returns the number of breaks; 0 for non-letters or a
/// word too long or short, in which case breaks must not be read.
int hyphen_en_breaks(const char* word, size_t len, uint8_t* breaks);
