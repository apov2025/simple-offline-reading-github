/* SPDX-License-Identifier: Apache-2.0
 * 字体缓存读盘计数与失败回归。/ Font cache I/O counts and failure regression. */
#include <unistd.h>
static ssize_t counted_read(int fd, void *dst, size_t n);
#define read counted_read
#define main compound_test_main
#include "font_compound_host_test.c"
#undef main
#undef read
static size_t calls, bytes;
static int fail_reads;
static ssize_t counted_read(int fd, void *dst, size_t n) {
    calls++;
    if (fail_reads) { fail_reads--; return -1; }
    ssize_t got = read(fd, dst, n);
    if (got > 0) bytes += (size_t)got;
    return got;
}
static void counts_reset(void) { calls = bytes = 0; }
static void check_block(unsigned block) {
    int slot = io_find(block * TTF_IO_BLOCK); assert(slot >= 0);
    assert(io_fill[slot] == TTF_IO_BLOCK);
    for (unsigned i = 0; i < TTF_IO_BLOCK; i++)
        assert(io_data[slot * TTF_IO_BLOCK + i] == (unsigned char)(block + i));
}
int main(void) {
    FILE *f = tmpfile(); assert(f);
    for (unsigned b = 0; b < 64; b++)
        for (unsigned i = 0; i < TTF_IO_BLOCK; i++) fputc((unsigned char)(b + i), f);
    assert(!fflush(f)); font_fd = fileno(f); font_file_pos = UINT32_MAX;
    assert(io_ensure()); io_reset();
    assert(io_load_run(0, 2)); check_block(0); check_block(1);
    counts_reset(); assert(io_load_run(0, 2));
    fprintf(stderr, "cached two-block run: calls=%zu bytes=%zu\n", calls, bytes);
    assert(calls == 0 && bytes == 0);
    io_reset(); assert(io_load_block(TTF_IO_BLOCK));
    counts_reset(); assert(io_load_run(0, 4));
    assert(bytes == 3 * TTF_IO_BLOCK && calls == 2);
    for (unsigned b = 0; b < 4; b++) check_block(b);
    /* 满缓存淘汰后，热块与冷缺口仍保持原字节。/ Preserve bytes across eviction and mixed runs. */
    io_reset();
    for (unsigned b = 0; b < TTF_IO_SLOTS; b++) assert(io_load_block(b * TTF_IO_BLOCK));
    counts_reset(); assert(io_load_run(30 * TTF_IO_BLOCK, 8));
    assert(bytes == 6 * TTF_IO_BLOCK);
    for (unsigned b = 30; b < 38; b++) check_block(b);
    /* 批量读取失败后沿用逐块回退。/ Retain block fallback after a failed bulk read. */
    io_reset(); assert(io_load_block(0)); fail_reads = 1;
    assert(io_load_run(TTF_IO_BLOCK, 2));
    for (unsigned b = 0; b < 3; b++) check_block(b);
    fail_reads = 3; assert(!io_load_run(4 * TTF_IO_BLOCK, 2));
    for (unsigned b = 0; b < 3; b++) check_block(b);
    assert(io_find(4 * TTF_IO_BLOCK) < 0 && io_find(5 * TTF_IO_BLOCK) < 0);
    /* 合并缓冲缺失仍逐块读取；尾部短读只缓存实际读到的数据。/ Missing run buffer and short EOF reads retain fallback semantics. */
    io_reset(); uint8_t *run = io_run_buf; io_run_buf = NULL;
    counts_reset(); assert(io_load_run(0, 2)); assert(calls == 2 && bytes == 2 * TTF_IO_BLOCK);
    io_run_buf = run; check_block(0); check_block(1);
    assert(!io_load_run(63 * TTF_IO_BLOCK, 2));
    check_block(0); check_block(1); check_block(63);
    assert(io_find(64 * TTF_IO_BLOCK) < 0);
    font_fd = -1; fclose(f); heap_caps_free(io_data); heap_caps_free(io_run_buf);
    puts("font I/O: cached/mixed/evicted/fallback/error bytes PASS"); return 0;
}
