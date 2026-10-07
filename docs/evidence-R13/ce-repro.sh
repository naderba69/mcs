#!/bin/bash
CE_BIN=../bin/multics-r82a-stats-x64
CE_GOOD=01020306112233660A0B0C21515253F6
CE_KEY=0102030405060708091011121314
cd /home/user/mcs/tests
start_srv() { stdbuf -o0 -e0 $CE_BIN -C $CE_CFG -v > $CE_LOG 2>&1 & srv=$!;
  for i in $(seq 1 25); do curl -s -m 2 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:15700/ 2>/dev/null | grep -q 200 && break; sleep 1; done;
  for i in $(seq 1 15); do python3 -c "import socket;s=socket.create_connection(('127.0.0.1',15703),2);s.close()" 2>/dev/null && break; sleep 1; done; }
kill_srv() { kill -9 $srv 2>/dev/null; sleep 1; pkill -9 -x multics 2>/dev/null; sleep 1; }

CE_CFG=/tmp/cr2-s1.cfg; CE_LOG=/tmp/cr2-s1.log
cat > $CE_CFG <<EOF
HTTP PORT: 15700
HTTP USER: admin
HTTP PASS: admin
HTTP TITLE: mcs-ce
TELNET PORT: 15701
TELNET USER: admin
TELNET PASS: admin
DCW STATS: ON
STATS-WINDOW: 2000
CACHE PORT: 15704
CACHE FILTER: OFF

CCCAM PORT: 15703
F: cxu cxp { cacheex_mode=3; }

[ cxg ]
CAID: 1884
PROVIDERS: 0
PORT: 15702
ENABLE CACHEEX: YES
DCW TIMEOUT: 9000
USER: u1 p1
EOF
echo "=== scenario 1 ==="
start_srv
NC_ECMS=1 ./.ncclient.bin 127.0.0.1 15702 u1 p1 $CE_KEY 0 1884 64 > /tmp/cr2-nc1.out 2>&1 & nc=$!
sleep 2
./.cxpeer.bin 127.0.0.1 15703 cxu cxp "push 1884 0 64 $CE_GOOD" > /tmp/cr2-peer1.out 2>&1
wait $nc
grep -q "DELIVERED A CONTROL WORD" /tmp/cr2-nc1.out && echo "S1 deliver OK" || echo "S1 deliver FAIL"
kill_srv
echo "leftover: $(pgrep -a -f 'multics-r82a-stats-x64' | wc -l)"

CE_CFG=/tmp/cr2-s2.cfg; CE_LOG=/tmp/cr2-s2.log
cat > $CE_CFG <<EOF
HTTP PORT: 15700
HTTP USER: admin
HTTP PASS: admin
HTTP TITLE: mcs-ce
TELNET PORT: 15701
TELNET USER: admin
TELNET PASS: admin
DCW STATS: ON
STATS-WINDOW: 2000
CACHE PORT: 15704
CACHE FILTER: OFF

DCWFILTER CHECKSUM: OFF
DCWFILTER REPEAT: OFF

DEFAULT CACHEEX LOCAL_ONLY: YES
DEFAULT CACHEEX BLOCK_FAKE_CW: YES
DEFAULT CACHEEX CWCHECK: 2

CCCAM PORT: 15703
F: cxu cxp { cacheex_mode=3; }

[ cxg ]
CAID: 1884
PROVIDERS: 0
PORT: 15702
ENABLE CACHEEX: YES
DCW TIMEOUT: 9000
USER: u1 p1
EOF
echo "=== scenario 2 ==="
start_srv
./.cxpeer.bin 127.0.0.1 15703 cxu cxp "push 1884 0 C8 $CE_GOOD" > /tmp/cr2-peer2a.out 2>&1
NC_ECMS=1 ./.ncclient.bin 127.0.0.1 15702 u1 p1 $CE_KEY 0 1884 64 > /tmp/cr2-nc2.out 2>&1 & nc=$!
sleep 2
./.cxpeer.bin 127.0.0.1 15703 cxu cxp "push 1884 0 64 $CE_GOOD bad" "sleep 500" "push 1884 0 64 $CE_GOOD" "sleep 500" "push 1884 0 64 $CE_GOOD" > /tmp/cr2-peer2.out 2>&1
wait $nc
grep -q "DELIVERED A CONTROL WORD" /tmp/cr2-nc2.out && echo "S2 deliver OK" || echo "S2 deliver FAIL"
python3 cedcw.py 127.0.0.1 15701 admin admin > /tmp/cr2-stats2.out
kill_srv
echo "=== S2 server log ==="; grep -n "CACHEEX\|CWCHECK\|deliver\|found cccam" /tmp/cr2-s2.log | head -30
