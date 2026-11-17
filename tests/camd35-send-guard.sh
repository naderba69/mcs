#!/bin/sh
# Run the camd35/cs378x send-length guard regression against production code.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Isrc \
  tests/camd35-send-guard.c src/aes.c src/md5.c src/crc32.c -o "$tmp/test"
"$tmp/test"
