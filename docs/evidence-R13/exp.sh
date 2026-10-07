#!/bin/bash
# $1 = label ; $2 = "with_c8"|"no_c8" ; $3 = "with_bad"|"no_bad"
LABEL=$1; W8=$2; WB=$3
CE_GOOD=01020306112233660A0B0C21515253F6
CE_KEY=0102030405060708091011121314
cd /home/user/mcs/tests
CFG=/tmp/exp-$LABEL.cfg; LOG=/tmp/exp-$LABEL.log
cat > $CFG <<EOF
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
stdbuf -o0 -e0 ../bin/multics-r82a-stats-x64 -C $CFG -v > $LOG 2>&1 & srv=$!
for i in $(seq 1 25); do curl -s -m 2 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:15700/ 2>/dev/null | grep -q 200 && break; sleep 1; done
for i in $(seq 1 15); do python3 -c "import socket;s=socket.create_connection(('127.0.0.1',15703),2);s.close()" 2>/dev/null && break; sleep 1; done
[ "$W8" = "with_c8" ] && ./.cxpeer.bin 127.0.0.1 15703 cxu cxp "push 1884 0 C8 $CE_GOOD" > /tmp/exp-$LABEL-c8.out 2>&1
NC_ECMS=1 ./.ncclient.bin 127.0.0.1 15702 u1 p1 $CE_KEY 0 1884 64 > /tmp/exp-$LABEL-nc.out 2>&1 & nc=$!
sleep 2
if [ "$WB" = "with_bad" ]; then
  ./.cxpeer.bin 127.0.0.1 15703 cxu cxp "push 1884 0 64 $CE_GOOD bad" "sleep 500" "push 1884 0 64 $CE_GOOD" "sleep 500" "push 1884 0 64 $CE_GOOD" > /tmp/exp-$LABEL-peer.out 2>&1
else
  ./.cxpeer.bin 127.0.0.1 15703 cxu cxp "push 1884 0 64 $CE_GOOD" "sleep 500" "push 1884 0 64 $CE_GOOD" > /tmp/exp-$LABEL-peer.out 2>&1
fi
wait $nc
grep -q "DELIVERED A CONTROL WORD" /tmp/exp-$LABEL-nc.out && echo "$LABEL: DELIVERED" || echo "$LABEL: STARVED"
grep -c "CW REUSE PROOF" $LOG | sed "s/^/$LABEL: reuse-proof lines = /"
grep -c "CW NEGATIVE MARK" $LOG | sed "s/^/$LABEL: negative marks = /"
kill -9 $srv 2>/dev/null; sleep 1
