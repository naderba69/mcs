#!/bin/sh
# Run the production Radegast duplicate-ECM search against provider collisions.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu99 -O2 -ffunction-sections -fdata-sections -Isrc \
  tests/ecm-radegast-identity.c -Wl,--gc-sections -o "$tmp/test"
"$tmp/test"
