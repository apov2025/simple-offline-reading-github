#!/bin/sh
# 在临时目录验证真实策略代码。/ Test real policy code in a temporary directory.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -I"$root/main/board" "$root/tools/reader_controls_host_test.c" "$root/main/board/reader_controls.c" -o "$tmp/test"
"$tmp/test"
