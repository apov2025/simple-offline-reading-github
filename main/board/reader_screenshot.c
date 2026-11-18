/* SPDX-License-Identifier: Apache-2.0
 * 同步写完截图后关闭文件；不卸载卡。/ Finish and close screenshots synchronously; never unmount the card.
 */
#include "reader_screenshot.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void le32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8 * i));
}

bool reader_screenshot_write(const char* dir, const char* stem, const uint8_t* fb,
    int width, int height, char* path, size_t cap) {
    if (!path || !cap) return false;
    path[0] = 0;
    if (!dir || !stem || !fb || width <= 0 || width > 4096 || height <= 0 || height > 4096 || width % 2) return false;
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) return false;
    char safe[96];
    size_t n = 0;
    for (; stem[n] && n < sizeof(safe) - 1; n++) {
        unsigned char c = (unsigned char)stem[n];
        safe[n] = c < 32 || strchr("\\/:*?\"<>|", c) ? '_' : (char)c;
    }
    safe[n] = 0;
    if (!n) strcpy(safe, "screenshot");
    int fd = -1;
    for (unsigned i = 0; i < 10000; i++) {
        int size = snprintf(path, cap, "%s/%s-%04u.bmp", dir, safe, i);
        if (size < 0 || (size_t)size >= cap) break;
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) { path[0] = 0; return false; }
    FILE* f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(path); path[0] = 0; return false; }
    const size_t stride = ((size_t)height + 7) / 8 * 4;
    uint8_t header[118] = {'B','M'};
    le32(header + 2, (uint32_t)(sizeof(header) + stride * width));
    le32(header + 10, sizeof(header)); le32(header + 14, 40);
    le32(header + 18, height); le32(header + 22, width);
    header[26] = 1; header[28] = 4;
    le32(header + 34, (uint32_t)(stride * width)); le32(header + 46, 16);
    for (unsigned i = 0; i < 16; i++) {
        header[54 + 4*i] = header[55 + 4*i] = header[56 + 4*i] = i * 17;
    }
    uint8_t* row = calloc(1, stride);
    bool ok = row && fwrite(header, 1, sizeof(header), f) == sizeof(header);
    for (int y = width - 1; y >= 0 && ok; y--) {
        memset(row, 0, stride);
        for (int x = 0; x < height; x++) {
            const uint8_t byte = fb[(size_t)(height - 1 - x) * width / 2 + y / 2];
            const uint8_t gray = y % 2 ? byte >> 4 : byte & 15;
            row[x / 2] |= x % 2 ? gray : gray << 4;
        }
        ok = fwrite(row, 1, stride, f) == stride;
    }
    free(row);
    if (fclose(f) != 0) ok = false;
    if (!ok) { unlink(path); path[0] = 0; }
    return ok;
}
