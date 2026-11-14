#!/bin/bash
# TASK 4.2 — bounded live soak.
#
#   make -C tests soak
#   SOAK_SECONDS=90 make -C tests soak     # harness check, not the acceptance run
#
# Declared limits, fixed before the run starts:
#   RSS absolute ceiling: 65536 kB
#   RSS growth after the 2-minute mark: 16384 kB
# A shorter run still applies the absolute ceiling. The growth check
# applies only when the run is long enough to have a 2-minute sample.
#
# Not part of `make all`.
set -u

cd "$(dirname "$0")" || exit 1
BIN=${SOAK_BIN:-../bin/multics-r82a-stats-x64}
SECONDS_WANTED=${SOAK_SECONDS:-1200}
RSS_CEILING=${SOAK_RSS_KB:-65536}
RSS_GROWTH=${SOAK_GROWTH_KB:-16384}
LOG=../docs/soak-4.2
mkdir -p "$LOG"

HTTP=17100
CACHE=17101
NPORT=17110
TPORT=17111
# peer ports 17102..17109
CW=11223366445566FF77889998AABBCC31
KEY=0102030405060708091011121314

FAIL=0
SRV=0
CLI=0
PEERS=""

note() { printf '%s\n' "$*"; }

cleanup() {
	if [ "$CLI" -ne 0 ]; then kill -TERM "$CLI" 2>/dev/null; fi
	if [ "$SRV" -ne 0 ]; then kill -TERM "$SRV" 2>/dev/null; fi
	for p in $PEERS; do kill -TERM "$p" 2>/dev/null; done
	sleep 1
	if [ "$CLI" -ne 0 ]; then kill -9 "$CLI" 2>/dev/null; fi
	if [ "$SRV" -ne 0 ]; then kill -9 "$SRV" 2>/dev/null; fi
	for p in $PEERS; do kill -9 "$p" 2>/dev/null; done
}
trap cleanup EXIT

if [ ! -f "$BIN" ]; then
	note "stats binary missing: $BIN"
	exit 1
fi
chmod a+x "$BIN" 2>/dev/null || true
if [ ! -x "$BIN" ]; then
	note "stats binary is not executable: $BIN"
	exit 1
fi
if [ ! -x ./.ncclient.bin ] || [ ! -x ./.cachepeer.bin ]; then
	note "harness binaries missing; build via make -C tests soak"
	exit 1
fi

# refuse to start if our ports are already taken
for port in $HTTP $CACHE $NPORT $TPORT 17102 17103 17104 17105 17106 17107 17108 17109; do
	if ss -ltn 2>/dev/null | grep -q ":$port " || ss -lun 2>/dev/null | grep -q ":$port "; then
		note "port $port is already in use"
		exit 1
	fi
done

{
	printf 'HTTP PORT: %s\nHTTP USER: admin\nHTTP PASS: admin\n' "$HTTP"
	printf 'HTTP TITLE: mcs-soak\n'
	printf 'TELNET PORT: %s\nTELNET USER: admin\nTELNET PASS: admin\n' "$TPORT"
	printf 'DCW STATS: ON\n'
	printf 'CACHE PORT: %s\n' "$CACHE"
	printf 'CACHE PEER: 127.0.0.1:17102 { csp=1 }\n'
	printf 'CACHE PEER: 127.0.0.1:17103 { csp=1 }\n'
	printf 'CACHE PEER: 127.0.0.1:17104 { csp=1 }\n'
	printf 'CACHE PEER: 127.0.0.1:17105 { csp=1 }\n'
	printf 'CACHE PEER: 127.0.0.1:17106 { csp=1 }\n'
	printf 'CACHE PEER: 127.0.0.1:17107 { csp=1 }\n'
	printf 'CACHE PEER: 127.0.0.1:17108 { csp=1 }\n'
	printf 'CACHE PEER: 127.0.0.1:17109 { csp=1 }\n'
	printf 'CACHE FILTER: ON\n\n'
	printf '[ soak ]\nCAID: 1884\nPORT: %s\nUSER: u1 p1\n' "$NPORT"
} > "$LOG/soak.cfg"

note "soak: ${SECONDS_WANTED}s, RSS ceiling ${RSS_CEILING} kB, growth after 2 min ${RSS_GROWTH} kB"
note "binary: $BIN"

start_peer() {
	port=$1
	name=$2
	shift 2
	# remaining args are env assignments, then the peer stays in the background
	env "$@" stdbuf -o0 -e0 ./.cachepeer.bin "$CACHE" "$port" "$CW" 1000000 \
		> "$LOG/peer-$name.log" 2>&1 &
	PEERS="$PEERS $!"
	note "  peer $name pid $! port $port"
}

start_peer 17102 reply
start_peer 17103 repush-same CP_REPUSH_MS=3000 CP_REPUSH_SAME=1
start_peer 17104 repush-rotate CP_REPUSH_MS=3000
start_peer 17105 mirror CP_MIRROR=1 CP_FRESH_KEY=1
start_peer 17106 forged CP_FORGED_EVERY=4 CP_REPUSH_MS=3000
start_peer 17107 cycle CP_CYCLE_MARK=2 CP_FRESH_KEY=1
start_peer 17108 short CP_SHORT_REPLY=1
start_peer 17109 stranger CP_STRANGER=1
sleep 1

stdbuf -o0 -e0 "$BIN" -C "$LOG/soak.cfg" > "$LOG/server.log" 2>&1 &
SRV=$!
note "  server pid $SRV"

code=000
i=0
while [ "$i" -lt 40 ]; do
	code=$(curl -s -m 2 -u admin:admin -o /dev/null -w '%{http_code}' "http://127.0.0.1:${HTTP}/" || true)
	if [ "$code" = "200" ]; then
		break
	fi
	if ! kill -0 "$SRV" 2>/dev/null; then
		break
	fi
	i=$((i + 1))
	sleep 1
done
if [ "$code" != "200" ]; then
	note "FAIL: HTTP did not answer 200 (got $code)"
	tail -20 "$LOG/server.log"
	exit 1
fi
note "  HTTP up"

i=0
while [ "$i" -lt 40 ]; do
	if grep -q "advertised card" "$LOG/peer-reply.log" 2>/dev/null; then
		break
	fi
	i=$((i + 1))
	sleep 0.5
done
if ! grep -q "advertised card" "$LOG/peer-reply.log" 2>/dev/null; then
	note "FAIL: reply peer never advertised a card"
	exit 1
fi
note "  reply peer advertised"

read_stats() {
	python3 - "$TPORT" "$1" <<'PY'
import socket, sys, time
port, path = int(sys.argv[1]), sys.argv[2]
s = socket.create_connection(("127.0.0.1", port), 3)
s.settimeout(2)
def send(line):
    s.sendall((line + "\r\n").encode())
    time.sleep(0.3)
buf = b""
send("admin")
send("admin")
send("dcwstats")
time.sleep(0.4)
try:
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        buf += chunk
except socket.timeout:
    pass
s.close()
open(path, "wb").write(buf)
if b"dcwstats:" not in buf:
    sys.exit(1)
PY
}

rss_of() {
	awk '/VmRSS/ { print $2 }' "/proc/$1/status" 2>/dev/null || echo 0
}

: > "$LOG/rss.txt"
: > "$LOG/client.log"
connect_fail=0
restarts=0
start_ts=$(date +%s)
deadline=$((start_ts + SECONDS_WANTED))
next_sample=$start_ts
base_rss=0
base_set=0

# One long-lived client. Reconnecting at once is refused: the server
# closes a second login for the same user for 60 seconds while the
# slot still looks busy. Hopping happens inside that one connection.
start_client() {
	sid=$(printf '%04X' $(( ($(date +%s) % 200) + 1 )))
	NC_HOP=1 NC_ECMS=4000 NC_ECM_GAP_MS=40 \
		./.ncclient.bin 127.0.0.1 "$NPORT" u1 p1 "$KEY" 0 1884 "$sid" \
		>> "$LOG/client.log" 2>&1 &
	CLI=$!
	note "  client pid $CLI"
}
start_client

while [ "$(date +%s)" -lt "$deadline" ]; do
	now=$(date +%s)
	if ! kill -0 "$SRV" 2>/dev/null; then
		note "FAIL: server died during the soak"
		FAIL=1
		break
	fi
	if ! kill -0 "$CLI" 2>/dev/null; then
		restarts=$((restarts + 1))
		if [ "$restarts" -gt 8 ]; then
			note "FAIL: client died too often"
			FAIL=1
			break
		fi
		note "  client exited; waiting out the 60s login window"
		sleep 65
		if [ "$(date +%s)" -ge "$deadline" ]; then
			break
		fi
		start_client
		continue
	fi
	if [ "$now" -ge "$next_sample" ]; then
		rss=$(rss_of "$SRV")
		printf '%s %s\n' "$((now - start_ts))" "$rss" >> "$LOG/rss.txt"
		if [ "$rss" -gt "$RSS_CEILING" ]; then
			note "FAIL: RSS ${rss} kB over ceiling ${RSS_CEILING}"
			FAIL=1
			break
		fi
		if [ $((now - start_ts)) -ge 120 ] && [ "$base_set" -eq 0 ]; then
			base_rss=$rss
			base_set=1
			note "  baseline RSS at 2 min: ${base_rss} kB"
		fi
		next_sample=$((now + 15))
	fi
	sleep 5
done

if [ "$CLI" -ne 0 ]; then
	kill -TERM "$CLI" 2>/dev/null
	sleep 1
	kill -9 "$CLI" 2>/dev/null
	CLI=0
fi
ecms=$(grep -c "stage 6: ECM" "$LOG/client.log" 2>/dev/null || true)
ecms=${ecms:-0}

elapsed=$(( $(date +%s) - start_ts ))
rss_end=$(rss_of "$SRV")
printf '%s %s\n' "$elapsed" "$rss_end" >> "$LOG/rss.txt"
rss_max=$(awk 'BEGIN { m=0 } { if ($2+0 > m) m=$2+0 } END { print m }' "$LOG/rss.txt")

if [ "$FAIL" -eq 0 ] && ! kill -0 "$SRV" 2>/dev/null; then
	note "FAIL: server not alive at the end"
	FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
	# Stop the peers before comparing the two readouts. A push that
	# lands between telnet and the page is not a counter bug.
	for p in $PEERS; do kill -TERM "$p" 2>/dev/null; done
	sleep 2
	if ! read_stats "$LOG/telnet-end.txt"; then
		note "FAIL: telnet dcwstats did not answer"
		FAIL=1
	fi
	curl -s -m 5 -u admin:admin "http://127.0.0.1:${HTTP}/" > "$LOG/page.html" || true
fi

# stop the storm before judging the log, but keep the server until checks done
# (cleanup trap kills it on exit)

if grep -E -i 'segmentation|SIGSEGV|backtrace|Aborted' "$LOG/server.log" >/dev/null 2>&1; then
	note "FAIL: server log contains a crash marker"
	FAIL=1
fi

min_ecms=$((SECONDS_WANTED / 6))
if [ "$min_ecms" -lt 8 ]; then min_ecms=8; fi
if [ "$ecms" -lt "$min_ecms" ]; then
	note "FAIL: only $ecms ECMs sent, wanted at least $min_ecms"
	FAIL=1
fi

if [ "$rss_max" -gt "$RSS_CEILING" ]; then
	note "FAIL: max RSS ${rss_max} kB over ceiling ${RSS_CEILING}"
	FAIL=1
fi
if [ "$base_set" -eq 1 ] && [ "$rss_end" -gt $((base_rss + RSS_GROWTH)) ]; then
	note "FAIL: RSS grew from ${base_rss} to ${rss_end} kB, allowance ${RSS_GROWTH}"
	FAIL=1
fi

# counters: telnet says ON, every name is a number, page has the same number
if [ "$FAIL" -eq 0 ]; then
	if ! grep -q "dcwstats: ON" "$LOG/telnet-end.txt"; then
		note "FAIL: counters are not ON"
		FAIL=1
	fi
	python3 - "$LOG/telnet-end.txt" "$LOG/page.html" <<'PY' || FAIL=1
import re, sys
tel, page = sys.argv[1], sys.argv[2]
text = open(tel, errors="replace").read()
html = open(page, errors="replace").read()
rows = re.findall(r"^([A-Za-z0-9/_-]+) (\d+)\r?$", text, re.M)
if len(rows) < 5:
    sys.stderr.write("FAIL: telnet did not list 5 counters\n")
    sys.exit(1)
total = 0
for name, count in rows:
    total += int(count)
    m = re.search(r"<td>%s</td><td>(\d+)</td>" % re.escape(name), html)
    if not m:
        sys.stderr.write("FAIL: page missing %s\n" % name)
        sys.exit(1)
    if m.group(1) != count:
        sys.stderr.write("FAIL: %s telnet %s page %s\n" % (name, count, m.group(1)))
        sys.exit(1)
if total <= 0:
    sys.stderr.write("FAIL: all counters are zero\n")
    sys.exit(1)
print("counters match, total %d" % total)
PY
fi

{
	echo "elapsed_s $elapsed"
	echo "ecms_sent $ecms"
	echo "connect_fail $connect_fail"
	echo "rss_max_kb $rss_max"
	echo "rss_end_kb $rss_end"
	echo "rss_base_kb $base_rss"
	echo "rss_ceiling_kb $RSS_CEILING"
	echo "rss_growth_kb $RSS_GROWTH"
	echo "result $([ "$FAIL" -eq 0 ] && echo PASS || echo FAIL)"
} > "$LOG/RESULT.txt"

note "elapsed ${elapsed}s, ECMs $ecms, max RSS ${rss_max} kB, end ${rss_end} kB"
if [ "$FAIL" -ne 0 ]; then
	note "SOAK: FAIL"
	exit 1
fi
note "SOAK: PASS"
exit 0
