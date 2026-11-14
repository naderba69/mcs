#!/bin/sh
# TSan session for the R22 field flavour under real ECM/cache load.
#
# The stock memcheck live session never compiles -DR22_LAYERS, so the
# candidate layers' shared tables only ever ran uninstrumented. This
# session builds the whole server with -fsanitize=thread -DR22_LAYERS
# and drives it with the existing network harnesses (dual-card
# DoubleCheck, CCcam trace, cache network, CCcam upstream, provider
# identity, config reload), capturing sanitizer reports via log_path
# (server stdout disappears into harness temp dirs; log_path does not).
#
# Usage: sh tests/tsan-r22-session.sh [scenario ...]
#   scenarios: dc cc cache up pi reload free   (default: all)
# Exit status is nonzero if any scenario fails or any race is reported.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="$ROOT/make-x64/x64/mem-tsan-r22"
WORK="$ROOT/.tsan-r22-session"
mkdir -p "$DEST" "$WORK"
export TSAN_OPTIONS="halt_on_error=0 log_path=$WORK/report"

echo "== building TSan+R22 server =="
base=$(make -s -C "$ROOT/make-x64" print-cflags | sed 's/-O2/-O1 -g -fno-omit-frame-pointer/' | sed 's/-std=gnu89/-fgnu89-inline/')
# port_gnu89.h (named by -include in the cflags) lives in make-x64/;
# compile from there so every TU finds it.
cd "$ROOT/make-x64"
srcs=$(awk '/^OBJECTS =/{f=1} f{print} f&&/^$/{exit}' "$ROOT/src/Makefile" | grep -o '[A-Za-z0-9_-]*\.o' | sort -u | sed 's/\.o$/.c/' | tr '\n' ' ')
for s in $srcs; do
    extra=""
    case $s in auto-tuning.c) extra="-std=gnu99";; esac
    # shellcheck disable=SC2086
    gcc -fsanitize=thread -DR22_LAYERS $extra $base -w -c "$ROOT/src/$s" -o "$DEST/${s%.c}.o" || { echo "COMPILE FAIL $s"; exit 1; }
done
# shellcheck disable=SC2086
gcc -fsanitize=thread $base -w -o "$DEST/multics" "$DEST"/*.o -pthread -lm || { echo "LINK FAIL"; exit 1; }
echo "built $DEST/multics"

rm -f "$WORK"/report.*
FAIL=0
run() {
    name=$1; shift
    echo "== scenario: $name =="
    if timeout 300 "$@" > "$WORK/$name.log" 2>&1; then
        echo "  ok $name"
    else
        echo "  FAIL $name (see $WORK/$name.log)"
        FAIL=1
    fi
}

scenarios=${*:-"dc cc cache up pi reload free"}
for sc in $scenarios; do
    case $sc in
        dc)     run dc     python3 "$ROOT/tests/doublecheck-live.py" "$DEST/multics" ;;
        cc)     run cc     python3 "$ROOT/tests/cccam-trace-success.py" "$DEST/multics" ;;
        cache)  run cache  python3 "$ROOT/tests/cache-trace-network.py" "$DEST/multics" ;;
        up)     run up     python3 "$ROOT/tests/cccam-upstream-trace.py" "$DEST/multics" ;;
        pi)     MODE=same run pi python3 "$ROOT/tests/ecm-provider-identity.py" "$DEST/multics" ;;
        reload) (cd "$ROOT/tests" && BIN="$DEST/multics" run reload sh newcamd-config-reload.sh) ;;
        free)   run free   python3 "$ROOT/tests/freecccam-live.py" "$DEST/multics" ;;
        *) echo "unknown scenario: $sc"; FAIL=1 ;;
    esac
done

races=$(grep -l "WARNING: ThreadSanitizer: data race" "$WORK"/report.* 2>/dev/null | wc -l)
total=$(grep -h "WARNING: ThreadSanitizer: data race" "$WORK"/report.* 2>/dev/null | wc -l)
echo "TSan data race warnings: $total (in $races report files)"
if [ "$total" -ne 0 ]; then
    grep -h "SUMMARY: ThreadSanitizer" "$WORK"/report.* | sort | uniq -c | sort -nr | head -20
    FAIL=1
fi
[ "$FAIL" -eq 0 ] && echo "TSAN-R22 SESSION: clean" || echo "TSAN-R22 SESSION: FAILED"
exit $FAIL
