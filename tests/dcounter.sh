#!/bin/sh
# Band 120 harness: DoubleCheck counts closures and timeouts apart.
# Costs ~19s of real ageing: the reclaim window is 2x DOUBLECHECK_TIMEOUT_MS and
# the header has no clock seam on purpose (DC_NOW_MS is GetTickCount in prod).
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu89 -Wall -Werror -O1 -Isrc \
  tests/dcounter.c -o "$tmp/test" -lpthread
"$tmp/test"
