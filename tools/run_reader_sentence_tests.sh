#!/bin/sh
# 中文：独立句界主机回归，无模拟器或设备。/ English: Standalone sentence tests, without simulator or device.
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Imain/annotations tools/reader_sentence_host_test.c main/annotations/reader_sentence.c -o "$out/test"
"$out/test"
