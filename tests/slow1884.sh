#!/bin/sh
# Band 66: why CAID 1884 "brings no CW" on this build while other
# copies open the channel - live A/B proof of the root cause.
#
# Field trace (2026-11-16): every 1884 attempt failed at ~1005 ms
# ("DCW timeout") while its Newcamd sources answered at 1858-5279 ms;
# the late CWs were then rejected stale/recycled. The mechanism is
# in th-ecm.c: ecm_get_effective_timeout() lets the provider table's
# fallback_timeout (1000 ms for 1884:000000, ecm-provider-config.h)
# silently cap the wait UNLESS the profile sets DCW TIMEOUT
# explicitly (config.c sets dcw.timeout_explicit, which wins).
# Stock/other copies have no such table, so the same slow card
# answers inside their window and the channel opens.
#
# This rig: a real card server (.ncserver.bin) that always answers
# after 2500 ms, one 1884 profile, one ECM from .ncclient.bin.
#   Phase A (no DCW TIMEOUT): client must get decode-failed (~1 s).
#   Phase B (DCW TIMEOUT: 5000): the SAME slow answer must deliver.
set -u
cd "$(dirname "$0")"
BIN=${BIN:-../bin/multics.x64}
HPORT=15700
NPORT=15711
CARD=15712
KEY=0102030405060708091011121314
WORK=$(mktemp -d)
CARD_PID=""
SRV_PID=""

cleanup() {
	[ -n "$CARD_PID" ] && kill -9 "$CARD_PID" 2>/dev/null
	[ -n "$SRV_PID" ] && kill -9 "$SRV_PID" 2>/dev/null
	rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

[ -x "$BIN" ] || { echo "slow1884: build first: make -C ../make-x64 release-stats"; exit 1; }
for b in .ncclient.bin .ncserver.bin; do
	[ -x "$b" ] || MAKEFLAGS= make -s --no-print-directory "$b" || { echo "slow1884: cannot build $b"; exit 1; }
done

cat > "$WORK/base.cfg" <<EOF
HTTP PORT: $HPORT
HTTP USER: admin
HTTP PASS: admin
TELNET PORT: 0
N: 127.0.0.1 $CARD srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14

[ slow1884 ]
CAID: 1884
PROVIDERS: 0
PORT: $NPORT
USER: u1 p1
EOF

NS_DELAYS=2500 stdbuf -o0 -e0 ./.ncserver.bin "$CARD" "$KEY" 1884 000000 > "$WORK/card.log" 2>&1 &
CARD_PID=$!

run_phase() { # cfgfile -> client output
	stdbuf -o0 -e0 "$BIN" -C "$1" > "$WORK/server.log" 2>&1 &
	SRV_PID=$!
	i=0
	while [ $i -lt 40 ]; do
		grep -q "connect to server" "$WORK/server.log" 2>/dev/null && break
		i=$((i+1)); sleep 0.25
	done
	NC_ECMS=1 ./.ncclient.bin 127.0.0.1 "$NPORT" u1 p1 "$KEY" 0 1884 0064 2>&1
	kill -9 "$SRV_PID" 2>/dev/null
	wait "$SRV_PID" 2>/dev/null
	SRV_PID=""
	sleep 1
}

fail=0
out=$(run_phase "$WORK/base.cfg")
echo "$out" | grep -q "decode-failed (all-zero)" \
	|| { echo "slow1884: FAIL: phase A must fail at the 1000ms provider cap"; fail=1; }

sed 's/USER: u1 p1/USER: u1 p1\nDCW TIMEOUT: 5000/' "$WORK/base.cfg" > "$WORK/explicit.cfg"
out=$(run_phase "$WORK/explicit.cfg")
echo "$out" | grep -q "THE SERVER DELIVERED A CONTROL WORD" \
	|| { echo "slow1884: FAIL: phase B must deliver the 2500ms answer"; fail=1; }

if [ $fail -eq 0 ]; then
	echo "slow1884: PASS (1884 capped at 1000ms by provider table; explicit DCW TIMEOUT: 5000 delivers the slow CW)"
else
	echo "slow1884: FAIL"
	exit 1
fi
