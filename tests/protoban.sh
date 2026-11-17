#!/bin/sh
# Run the Band 60 per-protocol ban regression against production loginthrottle.c.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Isrc \
  tests/protoban.c -lpthread -o "$tmp/test"
"$tmp/test"
