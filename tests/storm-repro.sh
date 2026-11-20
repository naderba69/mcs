#!/bin/bash
# Band 108 crash repro: the tests/soak.sh storm against a chosen server binary.
# Usage: storm-repro.sh <binary> <seconds>
BIN=${1:-../bin/multics.x64}
SECS=${2:-90}
cd "$(dirname "$0")"
CW=11223366445566FF77889998AABBCC31
ASAN_OPTIONS=abort_on_error=0:detect_leaks=0 "$BIN" -C ../docs/soak-4.2/soak.cfg > /tmp/storm-server.log 2>&1 &
SRV=$!
sleep 3
./.cachepeer.bin 17101 17102 $CW 1000000 > /tmp/peer1.log 2>&1 &
CP_REPUSH_MS=3000 CP_REPUSH_SAME=1 ./.cachepeer.bin 17101 17103 $CW 1000000 > /tmp/peer2.log 2>&1 &
CP_REPUSH_MS=3000 ./.cachepeer.bin 17101 17104 $CW 1000000 > /tmp/peer3.log 2>&1 &
CP_MIRROR=1 CP_FRESH_KEY=1 ./.cachepeer.bin 17101 17105 $CW 1000000 > /tmp/peer4.log 2>&1 &
CP_FORGED_EVERY=4 CP_REPUSH_MS=3000 ./.cachepeer.bin 17101 17106 $CW 1000000 > /tmp/peer5.log 2>&1 &
CP_CYCLE_MARK=2 CP_FRESH_KEY=1 ./.cachepeer.bin 17101 17107 $CW 1000000 > /tmp/peer6.log 2>&1 &
CP_SHORT_REPLY=1 ./.cachepeer.bin 17101 17108 $CW 1000000 > /tmp/peer7.log 2>&1 &
CP_STRANGER=1 ./.cachepeer.bin 17101 17109 $CW 1000000 > /tmp/peer8.log 2>&1 &
sleep 2
NC_HOP=1 NC_ECMS=4000 NC_ECM_GAP_MS=40 ./.ncclient.bin 127.0.0.1 17110 u1 p1 0102030405060708091011121314 0 1884 00C9 > /tmp/storm-client.log 2>&1 &
CLI=$!
echo "storm up: server=$SRV client=$CLI"
for i in $(seq 1 "$SECS"); do
  if ! kill -0 $SRV 2>/dev/null; then echo "SERVER DIED after ~${i}s"; break; fi
  sleep 1
done
kill -0 $SRV 2>/dev/null && echo "server survived ${SECS}s" || echo "server is DEAD"

# Phase 2 (band 108): the harness end-phase -- TERM the peers, telnet
# dcwstats, then fetch the home page, checking liveness after each step.
pkill -f 'cachepeer[.]bin' 2>/dev/null; sleep 2
kill -0 $SRV 2>/dev/null && echo "phase2: alive after peer TERM" || echo "phase2: DEAD after peer TERM"
python3 /dev/stdin <<'PYEOF'
import socket, time
try:
    s = socket.create_connection(("127.0.0.1", 17111), 3); s.settimeout(2)
    def send(l):
        s.sendall((l + "\r\n").encode()); time.sleep(0.3)
    buf = b""; send("admin"); send("admin"); send("dcwstats"); time.sleep(0.4)
    try:
        while True:
            c = s.recv(4096)
            if not c: break
            buf += c
    except socket.timeout: pass
    print("phase2: telnet answered" if b"dcwstats:" in buf else "phase2: telnet SILENT")
except Exception as e:
    print("phase2: telnet FAILED", e)
PYEOF
kill -0 $SRV 2>/dev/null && echo "phase2: alive after telnet" || echo "phase2: DEAD after telnet"
for i in 1 2 3; do
  curl -s -m 15 -u admin:admin -o /tmp/phase2-page$i.html -w "phase2: curl$i HTTP %{http_code} %{size_download}B\n" http://127.0.0.1:17100/
  kill -0 $SRV 2>/dev/null && echo "phase2: alive after curl$i" || { echo "phase2: DEAD after curl$i"; break; }
done
kill $CLI 2>/dev/null; pkill -f 'cachepeer[.]bin' 2>/dev/null; kill $SRV 2>/dev/null
sleep 1
echo "repro done"
