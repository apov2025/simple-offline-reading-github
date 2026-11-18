/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书库书目信息（书名、作者、年份）读取的主机测试。
 * English: Host test for reading listing metadata (title, author, year).
 * 用法 / Usage: test <epub> <书名 title> <作者 author> <年份 year>   或 / or   test --bad <file>
 * 冻结：仅用于主机测试。/ Frozen: Host tests only.
 */
#include "book_epub.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    book_epub_meta_t meta;
    if (argc == 3 && !strcmp(argv[1], "--bad")) {
        memset(&meta, 0x55, sizeof(meta));
        assert(book_epub_read_meta(argv[2], &meta) != ESP_OK);
        // 失败时三项都清空，调用方才能放心回退到文件名。/ All fields are cleared on failure so callers can fall back to the file name.
        assert(!meta.title[0] && !meta.author[0] && !meta.year[0]);
        printf("epub metadata rejection passed: %s\n", argv[2]);
        return 0;
    }
    assert(argc == 5);
    assert(book_epub_read_meta(argv[1], &meta) == ESP_OK);
    printf("%s: title=[%s] author=[%s] year=[%s]\n", argv[1], meta.title, meta.author, meta.year);
    assert(!strcmp(meta.title, argv[2]));
    assert(!strcmp(meta.author, argv[3]));
    assert(!strcmp(meta.year, argv[4]));
    book_epub_t *book = NULL;
    assert(book_epub_open(argv[1], &book) == ESP_OK);
    book_epub_meta_t opened;
    assert(book_epub_read_open_meta(book, &opened) == ESP_OK);
    assert(!memcmp(&meta, &opened, sizeof(meta)));
    uint8_t *mimetype = NULL;
    assert(book_epub_read_open_file(book, "mimetype", 20, &mimetype) == ESP_OK);
    assert(!memcmp(mimetype, "application/epub+zip", 20));
    free(mimetype);
    mimetype = (void *)1;
    assert(book_epub_read_open_file(book, "mimetype", 19, &mimetype) != ESP_OK && mimetype == NULL);
    assert(book_epub_read_open_file(book, "missing", 20, &mimetype) != ESP_OK && mimetype == NULL);
    assert(book_epub_read_open_file(NULL, "mimetype", 20, &mimetype) != ESP_OK && mimetype == NULL);
    memset(&opened, 0x55, sizeof(opened));
    assert(book_epub_read_open_meta(NULL, &opened) != ESP_OK && !opened.title[0]);
    assert(book_epub_read_open_meta(book, NULL) != ESP_OK);
    book_epub_close(book);

    assert(book_epub_read_meta(NULL, &meta) != ESP_OK && book_epub_read_meta(argv[1], NULL) != ESP_OK);
    return 0;
}
