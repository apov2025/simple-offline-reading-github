/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：英文断词的主机测试，对照 TeX 的标准结果。
 * English: Host test for English hyphenation against the standard TeX results.
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "hyphen_en.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// 把断点用连字符标出来，和期望值比。/ Mark the breaks with hyphens and compare with the expectation.
static void expect(const char* word, const char* want) {
    uint8_t breaks[HYPHEN_EN_WORD_MAX + 8] = { 0 };
    char got[2 * HYPHEN_EN_WORD_MAX + 16];
    size_t n = strlen(word), o = 0;
    hyphen_en_breaks(word, n, breaks);
    for (size_t i = 0; i < n; i++) {
        if (i < sizeof(breaks) && breaks[i]) got[o++] = '-';
        got[o++] = word[i];
    }
    got[o] = 0;
    printf("%-22s %s\n", word, got);
    assert(!strcmp(got, want));
}

int main(void) {
    expect("hyphenation", "hy-phen-ation");
    expect("typesetting", "type-set-ting");
    expect("knowledge", "knowl-edge");
    expect("computer", "com-puter");
    expect("algorithm", "al-go-rithm");
    expect("international", "in-ter-na-tional");
    expect("Understanding", "Un-der-stand-ing");   // 大写照样断 / capitals hyphenate too
    expect("the", "the");                          // 太短不断 / too short
    expect("through", "through");                  // 单音节 / one syllable
    expect("table", "ta-ble");                     // 例外词 / exception list
    expect("project", "project");                  // 例外词：不断 / exception: no break
    expect("associate", "as-so-ciate");
    expect("don't", "don't");                      // 含非字母：不处理 / non-letters: untouched
    expect("supercalifragilisticexpialidocious", "su-per-cal-ifrag-ilis-tic-ex-pi-ali-do-cious");
    expect("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
    puts("hyphenation tests passed");
    return 0;
}
