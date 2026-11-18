#!/bin/sh
# 截图真实文件测试，仅写临时目录。/ Real screenshot file tests, writing only temporary directories.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -I"$root/main/board" "$root/tools/reader_screenshot_host_test.c" "$root/main/board/reader_screenshot.c" -o "$tmp/test"
"$tmp/test" "$tmp/images"
