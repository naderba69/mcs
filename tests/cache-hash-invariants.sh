#!/bin/sh
# Run real ECM hash and cache pipe regressions under UBSan.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=gnu99 -O1 -fgnu89-inline -fsanitize=undefined -fno-sanitize-recover=all -fpack-struct -ffunction-sections -fdata-sections -Isrc \
  -DCLI_CSCACHE -DSRV_CSCACHE -DCHECK_NEXTDCW -DSID_FILTER -DNEWCACHE \
  -DCCCAM_CLI -DRADEGAST_CLI -DCAMD35_CLI -DCS378X_CLI -DHTTP_SRV \
  -DTELNET -DMGCAMD_SRV -DCCCAM_SRV -DCAMD35_SRV -DCS378X_SRV \
  -DFREECCCAM_SRV -DEXPIREDATE -DDCWSWAP -DCACHEEX -DTHREAD_DCW \
  -DMONOTHREAD_ACCEPT -DDEBUG_NETWORK -DEPOLL_NEWCAMD -DEPOLL_ECM \
  tests/cache-hash-invariants.c src/ecmdata.c -Wl,--gc-sections -o "$tmp/test"
"$tmp/test"
