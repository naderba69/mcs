#!/bin/sh
# Band 115: pin the per-channel last-served-CW memory (feed inputs,
# never verdicts; exact TTL; provid distinctness).
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu89 -Wall -Werror -O1 -Isrc \
  tests/chan-prevcw.c -o "$tmp/test"
"$tmp/test"
