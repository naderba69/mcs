#!/bin/sh
# Band 111: pin the shadow gate classifier against the estimator predicate.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu89 -Wall -Werror -O1 -Isrc \
  tests/gate-shadow-class.c -o "$tmp/test"
"$tmp/test"
