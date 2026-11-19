#!/bin/sh
# Band 112: pin the trace label mapping for cache peer-request gate values.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu89 -Wall -Werror -O1 -Isrc \
  tests/trace-labels.c -o "$tmp/test"
"$tmp/test"
