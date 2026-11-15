#!/bin/sh
# Run the Band 58 half-CW cross-channel reuse regression against production code.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Isrc \
  tests/cwreuse.c -lpthread -o "$tmp/test"
"$tmp/test"
