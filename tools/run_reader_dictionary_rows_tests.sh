#!/bin/sh
# 独立主机几何回归，不调用sim make。/ Standalone host geometry regression, without sim make.
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
strip=-Wl,--gc-sections
if [ "$(uname -s)" = Darwin ]; then strip=-Wl,-dead_strip; fi
cc -std=gnu17 -Wall -Wextra -Werror -Wno-unused-function -g -fsanitize=address,undefined -ffunction-sections -fdata-sections \
 -Isim/idf -Imain/dictionary -Imain/book -Imain/ui -Imain/font -Icomponents/epdiy/src -Icomponents/epdiy/include \
 tools/reader_dictionary_rows_host_test.c "$strip" -lm -o "$out/test"
"$out/test"
