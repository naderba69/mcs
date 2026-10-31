#!/bin/sh
# Isolated characterization of the current production cache-check behavior.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu99 -O2 -fpack-struct -ffunction-sections -fdata-sections -Isrc \
  -DCLI_CSCACHE -DSRV_CSCACHE -DCHECK_NEXTDCW -DSID_FILTER -DNEWCACHE \
  -DCCCAM_CLI -DRADEGAST_CLI -DCAMD35_CLI -DCS378X_CLI -DHTTP_SRV \
  -DTELNET -DMGCAMD_SRV -DCCCAM_SRV -DCAMD35_SRV -DCS378X_SRV \
  -DFREECCCAM_SRV -DEXPIREDATE -DDCWSWAP -DCACHEEX -DTHREAD_DCW \
  -DMONOTHREAD_ACCEPT -DDEBUG_NETWORK -DEPOLL_NEWCAMD -DEPOLL_ECM \
  tests/cache-cross-provider.c -Wl,--gc-sections -o "$tmp/test"
"$tmp/test"
