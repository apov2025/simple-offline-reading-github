/* SPDX-License-Identifier: Apache-2.0
 * 中文：目录测试的 ZIP 读取计数与分配故障注入。/ English: TOC ZIP extraction counts and allocation fault injection.
 * 冻结：只用于主机测试，底层 ZIP 仍执行真实读取与 CRC 校验。/ Frozen: Host tests only; real ZIP reads and CRC checks remain active.
 */
#include "zip_reader.h"
#include <stdbool.h>
#include <stdlib.h>
size_t toc_test_extracts;
int toc_test_fail_after = -1;
bool toc_test_failed;
static bool fail(void) {
    if (toc_test_fail_after < 0) return false;
    if (toc_test_fail_after > 0) { --toc_test_fail_after; return false; }
    toc_test_failed = true; return true;
}
void *book_test_malloc(size_t n) { return fail() ? NULL : malloc(n); }
void *book_test_calloc(size_t n, size_t size) { return fail() ? NULL : calloc(n, size); }
void *book_test_realloc(void *p, size_t n) { return fail() ? NULL : realloc(p, n); }
void book_test_free(void *p) { free(p); }
esp_err_t book_test_zip_extract_real(zip_reader_t *reader, int index, void *dst, size_t cap);
esp_err_t zip_extract(zip_reader_t *reader, int index, void *dst, size_t cap) {
    ++toc_test_extracts; return book_test_zip_extract_real(reader, index, dst, cap);
}
