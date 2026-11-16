#!/bin/sh
# Band 64: live proof of the band-63 mgcamd protoban feed.
#
# Band 63 shipped mgcamd wiring as "code-reviewed, not live-proven"
# because no mgcamd test client existed. It turns out none was
# needed: mgcamd speaks the same cs_message framing and
# des_login_key_get derivation as newcamd (srv-mgcamd.c uses
# cs_message_receive/send directly), so the existing ncclient
# harness drives it unchanged. This test uses it to prove, live:
#   - 2 wrong passwords then correct -> LOGIN_ACK (not yet banned)
#   - 3 wrong passwords then correct -> no 14-byte keymod at all
#     (banned at accept, before any protocol bytes)
#   - /user-trace audit ring shows the mgcamd attempts by name.
#
# Note: mgcamd closes wrong-credential connections WITHOUT sending
# the NAK (the send is commented out in srv-mgcamd.c), so a failed
# attempt presents as "stage 5: no usable login answer". That is
# the stock behaviour, not a test artifact.
set -u
cd "$(dirname "$0")"
BIN=${BIN:-../bin/multics.x64}
HPORT=15600
MPORT=15603
KEY=0102030405060708091011121314
WORK=$(mktemp -d)
SRVPID=""

cleanup() {
	[ -n "$SRVPID" ] && kill "$SRVPID" 2>/dev/null
	rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

[ -x "$BIN" ] || { echo "mgban: build first: make -C ../make-x64 release-stats"; exit 1; }
if [ ! -x .ncclient.bin ]; then
	MAKEFLAGS= make -s --no-print-directory .ncclient.bin || { echo "mgban: cannot build ncclient"; exit 1; }
fi

cat > "$WORK/cfg" <<EOF
HTTP PORT: $HPORT
HTTP USER: admin
HTTP PASS: admin

MGCAMD PORT: $MPORT
MGCAMD USER: mu1 mp1
MGCAMD LOGIN FAILS: 3
MGCAMD LOGIN BAN: 120

[ mgtest ]
CAID: 1884
PORT: 15601
USER: u1 p1
EOF

stdbuf -o0 -e0 "$BIN" -C "$WORK/cfg" > "$WORK/server.log" 2>&1 &
SRVPID=$!

i=0
while [ $i -lt 25 ]; do
	curl -s -m 1 -o /dev/null "http://127.0.0.1:$HPORT/" && break
	i=$((i+1)); sleep 0.2
done

attempt() { # user pass -> ncclient output
	NC_NO_ECM=1 ./.ncclient.bin 127.0.0.1 "$MPORT" "$1" "$2" "$KEY" 0 2>&1
}

fail=0
note() { echo "mgban: $1"; }

# Phase 1: two failures stay below the threshold; correct succeeds.
attempt mu1 wrong | grep -q "stage 5: no usable login answer" || { note "FAIL: wrong attempt 1 not rejected"; fail=1; }
attempt mu1 wrong | grep -q "stage 5: no usable login answer" || { note "FAIL: wrong attempt 2 not rejected"; fail=1; }
attempt mu1 mp1 | grep -q "stage 5: LOGIN_ACK" || { note "FAIL: correct login after 2 fails must succeed"; fail=1; }
sleep 2 # let the server notice the disconnect and clear the slot

# Phase 2: three failures trip the ban; correct creds get no keymod.
attempt mu1 wrong >/dev/null
attempt mu1 wrong >/dev/null
attempt mu1 wrong >/dev/null
attempt mu1 mp1 | grep -q "stage 1: server did not send the 14-byte init sequence" \
	|| { note "FAIL: banned IP must receive no keymod"; fail=1; }

# Phase 3: the audit ring on /user-trace names the mgcamd attempts.
curl -s -m 2 -u admin:admin "http://127.0.0.1:$HPORT/user-trace" > "$WORK/trace.html"
grep -q "<td>mgcamd</td><td>127.0.0.1</td><td>mu1</td><td>✅ ناجحة</td>" "$WORK/trace.html" \
	|| { note "FAIL: audit ring missing the mgcamd success"; fail=1; }
grep -q "<td>mgcamd</td><td>127.0.0.1</td><td>mu1</td><td>❌ فاشلة</td>" "$WORK/trace.html" \
	|| { note "FAIL: audit ring missing mgcamd failures"; fail=1; }

if [ $fail -eq 0 ]; then
	echo "mgban: PASS (mgcamd ban live: ACK below threshold, no keymod when banned, audit by name)"
else
	echo "mgban: FAIL"
	exit 1
fi
