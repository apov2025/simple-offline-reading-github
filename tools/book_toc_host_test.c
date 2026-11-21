/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实 EPUB 目录与正文定位回归。/ English: Real EPUB navigation and text-position regression.
 */
#include "book_source.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern size_t toc_test_extracts;
extern int toc_test_fail_after;
extern bool toc_test_failed;
static void advanced(const char *path, const char *mode) {
    size_t current = 999, chapter = 999, offset = 999;
    assert(book_chapter_count() == 4);
    size_t before = toc_test_extracts;
    if (!strcmp(mode, "allmissing")) {
        assert(book_toc_current(2, 0, &current) == ESP_ERR_NOT_FOUND && current == 999);
        assert(toc_test_extracts == before + 1);
        book_close(); return;
    }
    if (!strcmp(mode, "many")) {
        assert(book_toc_count() == 3000);
        assert(book_toc_current(1, 5998, &current) == ESP_OK && current == 2999);
        assert(toc_test_extracts == before + 1);
        for (size_t i = 0; i < 3000; ++i) {
            assert(book_toc_current(1, i * 2, &current) == ESP_OK && current == i);
            assert(book_toc_resolve(i, &chapter, &offset) == ESP_OK && chapter == 1 && offset == i * 2);
        }
        assert(toc_test_extracts == before + 1);
        book_close(); return;
    }
    assert(book_toc_count() == 7);
    const size_t chapters[] = {0, 1, 1, 1, 1, 2, 3, 3};
    const size_t offsets[] =  {0, 0, 1, 2, 3, 0, 0, 9};
    const size_t expected[] = {1, 1, 1, 4, 4, 4, 4, 6};
    for (size_t i = 0; i < sizeof(chapters) / sizeof(*chapters); ++i)
        assert(book_toc_current(chapters[i], offsets[i], &current) == ESP_OK && current == expected[i]);
    assert(toc_test_extracts == before + 2);
    html_text_t text = {0};
    assert(book_chapter_load_blocks(3, &text) == ESP_OK);
    assert(!text.blocks[0].image_repeated);
    html_text_free(&text); book_close();
    // 每个分配点失败后再试，不能留下部分锚点缓存或已读图片记录。
    // Retry after each allocation failure; no partial anchor cache or image-visit state may survive.
    bool reached_success = false;
    for (int fail = 0; fail < 100; ++fail) {
        assert(book_open(path) == ESP_OK);
        current = 999; toc_test_fail_after = fail; toc_test_failed = false;
        esp_err_t err = book_toc_current(1, 2, &current);
        bool failed = toc_test_failed; toc_test_fail_after = -1;
        if (err != ESP_OK) assert(current == 999);
        assert(book_toc_current(1, 2, &current) == ESP_OK && current == 4);
        book_close();
        if (!failed) { reached_success = true; break; }
    }
    assert(reached_success);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    assert(book_open(argv[1]) == ESP_OK);
    if (!strcmp(argv[2], "advanced") || !strcmp(argv[2], "allmissing") || !strcmp(argv[2], "many")) {
        advanced(argv[1], argv[2]); puts("current TOC, ties, gaps, cache and failure retry passed"); return 0;
    }
    assert(book_chapter_count() == 2);
    size_t total = book_total_bytes();
    size_t current = 999;
    assert(book_toc_current(2, 0, &current) == ESP_ERR_INVALID_ARG && current == 999);
    assert(book_toc_current(0, 0, NULL) == ESP_ERR_INVALID_ARG);
    bool fallback = !strcmp(argv[2], "fallback");
    assert(book_toc_count() == (fallback ? 2u : 7u));
    const char *titles[] = {"父 & 章", "第二节", "Inline", "Image", "Missing anchor", "Next", "EOF"};
    unsigned depths[] = {0, 1, 1, 1, 0, 0, 0};
    size_t offsets[] = {0, 8, 20, 26, 0, 9, 36};
    for (size_t i = 0; i < book_toc_count(); ++i) {
        book_toc_entry_t entry = {0};
        assert(book_toc_get(i, &entry) == ESP_OK);
        assert(entry.chapter == (fallback ? i : i == 5 ? 1u : 0u));
        if (!fallback) {
            if (i == 0 && !strcmp(argv[2], "longtitle")) {
                assert(strlen(entry.title) == 159);
                for (size_t n = 0; n < 159; n += 3) assert(!memcmp(entry.title + n, "中", 3));
            } else assert(!strcmp(entry.title, titles[i]));
            assert(entry.depth == depths[i]);
        }
        size_t chapter = 999, offset = 999;
        esp_err_t err = book_toc_resolve(i, &chapter, &offset);
        if (!fallback && (i == 4 || (i == 5 && !strcmp(argv[2], "corrupt_target")))) { assert(err != ESP_OK && chapter == 999 && offset == 999); continue; }
        assert(err == ESP_OK && chapter == entry.chapter);
        assert(offset == (fallback ? 0 : offsets[i]));
    }
    if (fallback) {
        assert(book_toc_current(1, 99999, &current) == ESP_OK && current == 1);
    } else {
        const size_t positions[] = {0, 7, 8, 19, 20, 25, 26, 35, 36, 99999};
        const size_t expected[] = {0, 0, 1, 1, 2, 2, 3, 3, 6, 6};
        for (size_t i = 0; i < sizeof(positions) / sizeof(*positions); ++i)
            assert(book_toc_current(0, positions[i], &current) == ESP_OK && current == expected[i]);
        if (strcmp(argv[2], "corrupt_target")) {
            assert(book_toc_current(1, 0, &current) == ESP_OK && current == 6);
            assert(book_toc_current(1, 9, &current) == ESP_OK && current == 5);
        } else {
            current = 999;
            assert(book_toc_current(1, 9, &current) != ESP_OK && current == 999);
        }
    }
    size_t chapter = 999, offset = 999;
    assert(book_toc_resolve(99999, &chapter, &offset) != ESP_OK && chapter == 999 && offset == 999);
    assert(book_total_bytes() == total);
    html_text_t body = {0};
    if (strcmp(argv[2], "corrupt_target")) {
        assert(book_chapter_load_blocks(1, &body) == ESP_OK);
        for (size_t i = 0; i < body.count; ++i) assert(!body.blocks[i].image_repeated);
        html_text_free(&body);
    }
    assert(book_chapter_load_blocks(0, &body) == ESP_OK);
    assert(!strcmp(body.utf8, "序 & A\n第二节\nX Y 中\n[图片]\nZ"));
    for (size_t i = 0; i < body.count; ++i) assert(!body.blocks[i].image_repeated);
    html_text_free(&body);
    book_close(); assert(book_toc_count() == 0);
    puts("toc offsets, nesting, fallback and source preservation passed");
}
