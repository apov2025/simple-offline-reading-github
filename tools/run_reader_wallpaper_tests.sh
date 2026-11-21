#!/bin/sh
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -g \
  -Itools/wallpaper_stubs -Itools/book_storage_stubs -Imain -Imain/board tools/reader_wallpaper_host_test.c -o "$out/test"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -fsanitize=address,undefined -g \
  -Itools/wallpaper_stubs -Imain tools/reader_wallpaper_settings_test.c -o "$out/settings"
cd "$out"
./test
./settings
