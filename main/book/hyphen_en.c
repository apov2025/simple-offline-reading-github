/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：Liang 断词算法。把单词两头加上句点，凡是规则表里能匹配上的子串，都把它带的数字叠到对应的
 * 字母间隙上（取大），最后数字为奇数的间隙可以断。规则表是生成的，见 hyphen_en_table.h。
 *
 * English: Liang's hyphenation algorithm. The word is wrapped in dots; every substring found in the
 * pattern table lays its digits over the matching letter gaps (keeping the maximum), and a gap whose
 * final digit is odd may break. The pattern table is generated; see hyphen_en_table.h.
 *
 * 冻结：不分配内存，不依赖字体或屏幕，主机测试可以直接链接。
 * Frozen: no allocation and no dependency on fonts or the display, so host tests link it directly.
 */
#include "hyphen_en.h"

#include <stdbool.h>
#include <string.h>

#include "hyphen_en_table.h"

#define LEFT_MIN 2
#define RIGHT_MIN 3

// 在规则表里二分查找长度为 n 的子串，返回它的数字串，找不到返回 NULL。
// Binary-search the table for a substring of length n; returns its digits, or NULL when absent.
static const uint8_t* find_pattern(const char* text, size_t n) {
    int lo = 0, hi = HYPH_PATTERN_COUNT - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const char* entry = (const char*)&k_hyph_pool[k_hyph_index[mid]];
        int cmp = strncmp(text, entry, n);
        if (cmp == 0 && entry[n] != 0) cmp = -1;  // 表里的更长，排在后面 / the entry is longer and sorts after
        if (cmp == 0) return (const uint8_t*)entry + n + 1;
        if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return NULL;
}

int hyphen_en_breaks(const char* word, size_t len, uint8_t* breaks) {
    if (word == NULL || breaks == NULL) return 0;
    if (len < LEFT_MIN + RIGHT_MIN || len > HYPHEN_EN_WORD_MAX) return 0;
    memset(breaks, 0, len);

    // 两头加句点的小写副本。/ Lowercase copy wrapped in dots.
    char buf[HYPHEN_EN_WORD_MAX + 3];
    buf[0] = '.';
    for (size_t i = 0; i < len; i++) {
        char c = word[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c < 'a' || c > 'z') return 0;
        buf[i + 1] = c;
    }
    buf[len + 1] = '.';
    buf[len + 2] = 0;
    const size_t n = len + 2;

    int count = 0;
    // 例外词直接按表里标的断。/ Exception words break exactly where the list says.
    for (size_t e = 0; e < sizeof(k_hyph_exceptions) / sizeof(k_hyph_exceptions[0]); e++) {
        const char* p = k_hyph_exceptions[e];
        size_t i = 0;
        uint8_t marks[HYPHEN_EN_WORD_MAX] = { 0 };
        bool same = true;
        for (; *p && same; p++) {
            if (*p == '-') { if (i < len) marks[i] = 1; continue; }
            same = i < len && buf[i + 1] == *p;
            i++;
        }
        if (!same || i != len) continue;
        for (size_t k = LEFT_MIN; k + RIGHT_MIN <= len; k++) {
            if (marks[k]) { breaks[k] = 1; count++; }
        }
        return count;
    }

    // levels[p] 是 buf[p] 之前那个间隙的数字。/ levels[p] is the digit of the gap before buf[p].
    uint8_t levels[HYPHEN_EN_WORD_MAX + 4] = { 0 };
    for (size_t i = 0; i < n; i++) {
        for (size_t l = 1; l <= HYPH_PATTERN_MAX_LETTERS && i + l <= n; l++) {
            const uint8_t* digits = find_pattern(buf + i, l);
            if (digits == NULL) continue;
            for (size_t j = 0; j <= l; j++) {
                if (digits[j] > levels[i + j]) levels[i + j] = digits[j];
            }
        }
    }
    // 第 k 个字母之前的间隙是 buf 里的位置 k + 1。/ The gap before letter k is position k + 1 in buf.
    for (size_t k = LEFT_MIN; k + RIGHT_MIN <= len; k++) {
        if (levels[k + 1] & 1) { breaks[k] = 1; count++; }
    }
    return count;
}
