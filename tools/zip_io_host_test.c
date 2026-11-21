/* SPDX-License-Identifier: Apache-2.0
 * ZIP大小查询与错误契约回归。/ ZIP size lookup and error-contract regression. */
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
static int eof_seeks, fail_seek, fail_stat, fake_size;
static int counted_seek(FILE *f, long off, int origin) {
    if (origin == SEEK_END) eof_seeks++;
    return fail_seek ? -1 : fseek(f, off, origin);
}
static int counted_stat(int fd, struct stat *st) {
    if (fail_stat) return -1;
    int result = fstat(fd, st);
    if (!result && fake_size == 1) st->st_size = -1;
    if (!result && fake_size == 2) st->st_size = (off_t)INT32_MAX + 1;
    return result;
}
#define fseek counted_seek
#define fstat counted_stat
#include "../main/book/zip_reader.c"
#undef fseek
#undef fstat
int main(int argc, char **argv) {
    assert(argc == 2); zip_reader_t *z = NULL;
    assert(zip_open(argv[1], &z) == ESP_OK && z); zip_close(z);
    fprintf(stderr, "ZIP open EOF seeks=%d\n", eof_seeks); assert(eof_seeks == 0);
    fail_stat = 1; assert(zip_open(argv[1], &z) == ESP_ERR_INVALID_SIZE && !z); fail_stat = 0;
    for (fake_size = 1; fake_size <= 2; fake_size++)
        assert(zip_open(argv[1], &z) == ESP_ERR_INVALID_SIZE && !z);
    fake_size = 0; fail_seek = 1;
    assert(zip_open(argv[1], &z) == ESP_ERR_INVALID_SIZE && !z);
    puts("ZIP I/O: no EOF seek, stat/size/seek failures PASS"); return 0;
}
