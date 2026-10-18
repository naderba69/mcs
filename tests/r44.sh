#!/bin/sh
# TASK R44 (D107) -- live proof of DCW VALIDECMTIME and ENABLE ECMLOOPING.
#
#   usage: sh r44.sh <multics-binary> <ncserver-binary> <ccdcw-binary>
#
# Every case is a fresh server, one real CCcam client asking one ECM, and a
# real newcamd card server (ncserver) with a scripted delay (NS_DELAYS) and
# an optional number of keyless replies (NS_NOK_FIRST).
#   A  late key refused    : card answers in 1500 ms, profile limit 800 ms
#   B  control             : same 1500 ms, no limit -> the key is served (stock)
#   C  inside the limit    : 300 ms, limit 800 ms -> served
#   D  loop off (stock)    : first reply keyless -> no key (no second round)
#   E  loop on             : first reply keyless -> round 2 serves the key
#   F  loop bounded        : every reply keyless -> exactly 3 sends, then stop
# Ports 16700-16705, outside the other targets' ranges.

BIN="$1"; NCS="$2"; CCD="$3"
HP=16700; CP=16701; CARD=16702; NP=16703
KEY=0102030405060708091011121314
CFG=.r44.cfg; SRVLOG=.r44-srv.log; NSLOG=.r44-ns.log; CLILOG=.r44-cli.log
ok=1

SRV_PID=""; NS_PID=""
cleanup() { [ -n "$SRV_PID" ] && kill -9 $SRV_PID 2>/dev/null; [ -n "$NS_PID" ] && kill -9 $NS_PID 2>/dev/null; SRV_PID=""; NS_PID=""; sleep 1; }

# case NAME DELAYS NOK_FIRST PROFILE_EXTRA_LINES
case_run() {
  name="$1"; delays="$2"; nok="$3"; extra="$4"
  rm -f $CFG $SRVLOG $NSLOG $CLILOG
  { printf 'HTTP PORT: %s\nHTTP USER: a\nHTTP PASS: a\nTELNET PORT: 0\n' $HP
    printf 'DEFAULT DCW CHECK: YES\nCCCAM PORT: %s\nF: u1 p1\n' $CP
    printf 'N: 127.0.0.1 %s srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n' $CARD
    printf '[ r44 ]\nCAID: 0500\nPROVIDERS: 0\nPORT: %s\nDCW TIMEOUT: 6000\nDCW CHECK: YES\nRETRY NEWCAMD: 0\n' $NP
    [ -n "$extra" ] && printf '%b\n' "$extra"
    printf 'USER: u1 p1\n\n'; } > $CFG
  NS_DELAYS=$delays NS_NOK_FIRST=$nok NS_VERBOSE=1 stdbuf -o0 -e0 "$NCS" $CARD $KEY 0500 000000 > $NSLOG 2>&1 &
  NS_PID=$!
  sleep 1
  stdbuf -o0 -e0 "$BIN" -C $CFG -v > $SRVLOG 2>&1 &
  SRV_PID=$!
  for i in $(seq 1 30); do grep -q "connect to server" $SRVLOG && break; sleep 1; done
  sleep 3
  timeout 25 "$CCD" 127.0.0.1 $CP u1 p1 0500 40 0x80 > $CLILOG 2>&1
  cleanup
  cp $SRVLOG .r44-$name.log
  dcw=$(grep -c ' DCW' $CLILOG)
  sends=$(grep -c 'NCSERVER: reply' $NSLOG)
  echo "  $name: client DCW=$dcw, card replies=$sends"
  echo "$name $dcw $sends" >> .r44-results.txt
}

rm -f .r44-results.txt
cleanup

case_run A_late    1500 0 "DCW VALIDECMTIME: 800"
case_run B_control 1500 0 ""
case_run C_inside  300  0 "DCW VALIDECMTIME: 800"
case_run D_loopoff 60   1 ""
case_run E_loopon  60   1 "ENABLE ECMLOOPING: YES"
case_run F_bounded 60   99 "ENABLE ECMLOOPING: YES"

get() { awk -v n="$1" '$1==n {print $2" "$3}' .r44-results.txt; }

a=$(get A_late); b=$(get B_control); c=$(get C_inside); d=$(get D_loopoff); e=$(get E_loopon); f=$(get F_bounded)
set -- $a; [ "$1" = 0 ] && echo "  [ ok ] A: a key that arrives after the limit is refused" || { echo "[FAIL] A: $1 key(s) served past the limit"; ok=0; }
set -- $b; [ "$1" = 1 ] && echo "  [ ok ] B: stock (no limit) serves the same late key" || { echo "[FAIL] B: stock served $1 keys"; ok=0; }
set -- $c; [ "$1" = 1 ] && echo "  [ ok ] C: a key inside the limit is served" || { echo "[FAIL] C: $1 keys inside the limit"; ok=0; }
set -- $d; [ "$1" = 0 ] && [ "$2" = 1 ] && echo "  [ ok ] D: loop off: one keyless reply, no second round (stock)" || { echo "[FAIL] D: dcw=$1 sends=$2"; ok=0; }
set -- $e; [ "$1" = 1 ] && [ "$2" = 2 ] && echo "  [ ok ] E: loop on: round 2 to the same server serves the key" || { echo "[FAIL] E: dcw=$1 sends=$2"; ok=0; }
set -- $f; [ "$1" = 0 ] && [ "$2" = 3 ] && echo "  [ ok ] F: loop bounded: 1 send + 2 rounds, then it stops" || { echo "[FAIL] F: dcw=$1 sends=$2 (expect 0 and 3)"; ok=0; }

grep -q "\[VALIDECMTIME\]" .r44-A_late.log && echo "  [ ok ] A: the server logs the late-key refusal" || { echo "[FAIL] A: no VALIDECMTIME line in the server log"; ok=0; }
grep -q "\[ECMLOOPING\] ch" .r44-E_loopon.log && echo "  [ ok ] E: the server logs the loop round" || { echo "[FAIL] E: no ECMLOOPING line"; ok=0; }
grep -q "\[ECMLOOPING\]" .r44-D_loopoff.log && { echo "[FAIL] D: loop ran although it is off"; ok=0; } || echo "  [ ok ] D: no loop line when the option is off"

# success: remove the scratch files; a failure keeps every log for diagnosis
if [ "$ok" = 1 ]; then rm -f $CFG $SRVLOG $NSLOG $CLILOG .r44-*.log .r44-results.txt; fi
[ "$ok" = 1 ]
