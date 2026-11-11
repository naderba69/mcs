#!/bin/sh
# Isolated production-code test for cross-cache provider identity guards.
# Exercise both internal pipe layouts, with and without CACHEEX.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

build_and_run() {
  name=$1
  shift
  ${CC:-cc} -std=gnu99 -O2 -pthread -fpack-struct -fgnu89-inline -ffunction-sections -fdata-sections -Isrc \
    -DCLI_CSCACHE -DSRV_CSCACHE -DCHECK_NEXTDCW -DSID_FILTER -DNEWCACHE \
    -DCCCAM_CLI -DRADEGAST_CLI -DCAMD35_CLI -DCS378X_CLI -DHTTP_SRV \
    -DTELNET -DMGCAMD_SRV -DCCCAM_SRV -DCAMD35_SRV -DCS378X_SRV \
    -DFREECCCAM_SRV -DEXPIREDATE -DDCWSWAP -DTHREAD_DCW \
    -DMONOTHREAD_ACCEPT -DDEBUG_NETWORK -DEPOLL_NEWCAMD -DEPOLL_ECM \
    "$@" tests/cache-cross-provider.c src/sockets.c src/cache-window.c src/newcamd-lifetime.c src/ecm-provider-config.c src/ecmdata.c src/ecm-optimizations.c src/advanced-optimizations.c src/biss.c src/caid-optimizations.c -Wl,--gc-sections -Wl,--wrap=clock_gettime \
    -Wl,--wrap=poll -Wl,--wrap=send -o "$tmp/test-$name"
  "$tmp/test-$name"
}

build_and_run cacheex -DCACHEEX
build_and_run no-cacheex
