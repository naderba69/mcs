#!/bin/sh
# Run the Band 55 MARCHCHECK regression against production code.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Isrc \
  tests/marchcheck.c -lpthread -o "$tmp/test"
"$tmp/test"
