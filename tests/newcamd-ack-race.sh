#!/bin/sh
# Drive the real Newcamd LOGIN_ACK send into a saturated TCP send queue, then
# prove client-state inspection and a second login progress while the first
# nonblocking send is waiting for POLLOUT.
set -eu
cd "$(dirname "$0")"

BIN=${BIN:-../make-x64/x64/multics}
CC=${CC:-cc}
[ -x "$BIN" ] || { echo "build first: make -C ../make-x64"; exit 1; }
command -v curl >/dev/null || { echo "curl required"; exit 1; }
command -v stdbuf >/dev/null || { echo "stdbuf required"; exit 1; }
command -v python3 >/dev/null || { echo "python3 required"; exit 1; }
command -v awk >/dev/null || { echo "awk required"; exit 1; }

tmp=$(mktemp -d)
server_pid=
first_pid=
second_pid=
retry_pid=
stop_pid() {
    pid=$1
    [ -n "$pid" ] || return 0
    kill "$pid" 2>/dev/null || true
    sleep 0.1
    if kill -0 "$pid" 2>/dev/null; then kill -9 "$pid" 2>/dev/null || true; fi
    wait "$pid" 2>/dev/null || true
}
cleanup() {
    if [ -n "$tmp" ]; then : > "$tmp/release" 2>/dev/null || true; fi
    stop_pid "$retry_pid"
    stop_pid "$second_pid"
    stop_pid "$first_pid"
    stop_pid "$server_pid"
    if [ "${KEEP_TEST_TMP:-0}" = 1 ]; then
        echo "[debug] preserved Newcamd ACK test files at $tmp"
    else
        rm -rf "$tmp"
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

ports=$(python3 - <<'PY'
import socket
sockets = []
try:
    for _ in range(2):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
    print(sockets[0].getsockname()[1], sockets[1].getsockname()[1])
finally:
    for sock in sockets:
        sock.close()
PY
)
set -- $ports
http_port=$1
newcamd_port=$2

$CC -std=gnu99 -O2 -fPIC -shared -pthread \
    -o "$tmp/newcamd-sendfault.so" newcamd-sendfault.c -ldl
$CC -O2 -m64 -std=gnu89 -w -o "$tmp/ncclient" \
    ncclient.c ../src/des.c ../src/msg-newcamd.c ../src/md5.c

cat > "$tmp/ack-race.cfg" <<EOF
HTTP PORT: $http_port
HTTP USER: admin
HTTP PASS: admin
HTTP TITLE: Newcamd ACK regression

[ ackrace ]
CAID: 1884
PORT: $newcamd_port
USER: u1 p1
EOF

LD_PRELOAD="$tmp/newcamd-sendfault.so" \
MCS_TEST_NEWCAMD_PORT="$newcamd_port" \
MCS_ACK_BLOCK_MARKER="$tmp/blocked" \
MCS_ACK_RELEASE_FILE="$tmp/release" \
MCS_ACK_SEND_RESULT="$tmp/ack-send-result" \
MCS_ACK_REFILL_MARKER="$tmp/refilled" \
MCS_ACK_POLL_ENTER="$tmp/poll-enter" \
MCS_ACK_POLL_RESULT="$tmp/ack-poll-result" \
    "$BIN" -C "$tmp/ack-race.cfg" > "$tmp/server.out" 2>&1 &
server_pid=$!

code=000
i=0
while [ "$i" -lt 100 ]; do
    code=$(curl -s -m 1 -u admin:admin -o /dev/null -w '%{http_code}' \
        "http://127.0.0.1:$http_port/" 2>/dev/null || true)
    [ "$code" = 200 ] && break
    kill -0 "$server_pid" 2>/dev/null || break
    sleep 0.1
    i=$((i + 1))
done
[ "$code" = 200 ] || { echo "[FAIL] test server did not start (HTTP $code)"; exit 1; }

wait_file() {
    path=$1
    limit=${2:-200}
    count=0
    while [ "$count" -lt "$limit" ]; do
        [ -f "$path" ] && return 0
        kill -0 "$server_pid" 2>/dev/null || return 1
        sleep 0.05
        count=$((count + 1))
    done
    return 1
}

wait_client_stage5() {
    path=$1
    limit=${2:-100}
    count=0
    while [ "$count" -lt "$limit" ]; do
        if grep -Eq 'stage 5: (LOGIN_ACK|LOGIN_NAK|no usable login answer|unexpected reply)' "$path" 2>/dev/null; then
            return 0
        fi
        kill -0 "$server_pid" 2>/dev/null || return 1
        sleep 0.05
        count=$((count + 1))
    done
    return 1
}

env NC_RCVBUF=256 \
    NC_LOGIN_SENT_FILE="$tmp/first-login-sent" \
    NC_WAIT_AFTER_LOGIN="$tmp/blocked" \
    NC_CLOSE_RELEASE_FILE="$tmp/release" \
    NC_CLOSE_MARKER="$tmp/first-rst" \
    NC_CLOSE_DELAY_MS=2000 \
    stdbuf -o0 -e0 "$tmp/ncclient" 127.0.0.1 "$newcamd_port" \
        u1 p1 0102030405060708091011121314 0 \
        > "$tmp/first-client.out" 2>&1 &
first_pid=$!

wait_file "$tmp/first-login-sent" 200 || {
    echo "[FAIL] first client did not send its login"; exit 1;
}
wait_file "$tmp/blocked" 200 || {
    echo "[FAIL] ACK test shim did not reach its send-queue barrier"; exit 1;
}
grep -q 'saturated=1' "$tmp/blocked" || {
    echo "[FAIL] real TCP send queue did not reach EAGAIN"; exit 1;
}
grep -q 'configured=1 read=1' "$tmp/blocked" || {
    echo "[FAIL] could not set and verify the controlled TCP send buffer"; exit 1;
}

env NC_NO_ECM=1 \
    NC_LOGIN_SENT_FILE="$tmp/second-login-sent" \
    stdbuf -o0 -e0 "$tmp/ncclient" 127.0.0.1 "$newcamd_port" \
        u1 p1 0102030405060708091011121314 0 \
        > "$tmp/second-client.out" 2>&1 &
second_pid=$!
wait_file "$tmp/second-login-sent" 200 || {
    echo "[FAIL] concurrent client did not send its login"; exit 1;
}
wait_client_stage5 "$tmp/second-client.out" 20 || {
    echo "[FAIL] duplicate login did not resolve promptly while the first ACK was held"; exit 1;
}
if wait "$second_pid"; then second_rc=0; else second_rc=$?; fi
second_pid=
[ "$second_rc" -ne 0 ] && ! grep -q 'stage 5: LOGIN_ACK' "$tmp/second-client.out" || {
    echo "[FAIL] a duplicate session received LOGIN_ACK while another ACK was pending"; exit 1;
}

touch "$tmp/release"
wait_file "$tmp/refilled" 200 || {
    echo "[FAIL] ACK shim did not report its immediate pre-ACK refill"; exit 1;
}
grep -q 'saturated=1' "$tmp/refilled" || {
    echo "[FAIL] send queue reopened before the real LOGIN_ACK send"; exit 1;
}
wait_file "$tmp/poll-enter" 100 || {
    echo "[FAIL] production send_nonb did not enter its POLLOUT wait"; exit 1;
}
grep -q 'timeout=100' "$tmp/poll-enter" || {
    echo "[FAIL] ACK POLLOUT wait did not use the production 100 ms poll interval"; exit 1;
}

# This existing authenticated page takes the same client-state lock as the
# runtime snapshot. The helper waits for a live ACK poll (entered but not yet
# returned), then measures both the poll-relative request start and full page
# response. A page sampled after the 100 ms poll has already returned fails.
page_stats=$(python3 - "$tmp/poll-enter" "$tmp/ack-poll-result" "$http_port" <<'PY'
import base64
import http.client
import sys
import time

enter_path, result_path, port = sys.argv[1], sys.argv[2], int(sys.argv[3])

def fields(path):
    try:
        with open(path, "r", encoding="ascii") as marker:
            text = marker.read().strip()
    except OSError:
        return {}
    return dict(item.split("=", 1) for item in text.split() if "=" in item)

def poll_state():
    entered = fields(enter_path)
    completed = fields(result_path)
    seq = int(entered.get("seq", "0"))
    result_seq = int(completed.get("seq", "0"))
    entered_us = int(entered.get("entered_us", "0"))
    timeout = int(entered.get("timeout", "0"))
    return seq, result_seq, entered_us, timeout

# POLLOUT can be immediately ready. If so, wait for the next guarded retry's
# poll marker; only a marker whose sequence has no completion proves that the
# production poll call is presently outstanding.
deadline = time.monotonic() + 2.0
while True:
    seq, result_seq, entered_us, timeout = poll_state()
    if seq > 0 and result_seq < seq:
        break
    if time.monotonic() >= deadline:
        print("000 999999999 999999999 0 0 0 0")
        sys.exit(0)
    time.sleep(0.001)

active_at_start = 1
request_start_us = time.monotonic_ns() // 1000
poll_offset_us = request_start_us - entered_us
status = 0
body = b""
connection = None
started_ns = time.monotonic_ns()
try:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2.0)
    authorization = base64.b64encode(b"admin:admin").decode("ascii")
    connection.request("GET", "/newcamd?pid=1&list=all",
                       headers={"Authorization": "Basic " + authorization})
    response = connection.getresponse()
    status = response.status
    body = response.read()
except Exception:
    status = 0
finally:
    if connection is not None:
        connection.close()
elapsed_us = (time.monotonic_ns() - started_ns) // 1000

seq_after, result_after, _, _ = poll_state()
active_at_end = int(seq_after > result_after and seq_after > 0)
page_ok = int(b"<title>Multi CardServer - Newcamd</title>" in body and
              b"ackrace" in body)
print("%d %d %d %d %d %d %d" %
      (status, elapsed_us, poll_offset_us, page_ok,
       active_at_start, active_at_end, seq_after))
PY
)
set -- $page_stats
page_code=${1:-000}
page_time_us=${2:-999999999}
poll_offset_us=${3:-999999999}
page_ok=${4:-0}
poll_active_start=${5:-0}
poll_active_end=${6:-0}
poll_seq=${7:-0}
page_time_ms=$(awk -v us="$page_time_us" 'BEGIN {printf "%.3f", us / 1000}')
[ "$page_code" = 200 ] && [ "$page_ok" = 1 ] && [ "$poll_seq" -gt 0 ] &&
    [ "$poll_active_start" = 1 ] && [ "$poll_active_end" = 1 ] &&
    [ "$poll_offset_us" -ge 0 ] && [ "$poll_offset_us" -lt 100000 ] &&
    [ $((poll_offset_us + page_time_us)) -lt 100000 ] || {
    echo "[FAIL] Newcamd page was not served during a live ACK poll (HTTP ${page_code}, ${page_time_ms} ms, poll offset ${poll_offset_us} us)"; exit 1;
}

wait_poll_timeout() {
    count=0
    while [ "$count" -lt 100 ]; do
        if grep -q 'timeout=100 result=0' "$tmp/ack-poll-result" 2>/dev/null; then
            return 0
        fi
        sleep 0.05
        count=$((count + 1))
    done
    return 1
}
wait_poll_timeout || {
    echo "[FAIL] production ACK poll did not reach its 100 ms timeout"; exit 1;
}
grep -q 'result=-1 errno=11' "$tmp/ack-send-result" || {
    echo "[FAIL] ACK send did not observe socket backpressure (EAGAIN)"; exit 1;
}

retry_attempt=0
retry_succeeded=0
retry_client_out=
while [ "$retry_attempt" -lt 10 ] && [ ! -e "$tmp/first-rst" ]; do
    retry_attempt=$((retry_attempt + 1))
    retry_client_out="$tmp/retry-client-$retry_attempt.out"
    retry_ack="$tmp/retry-ack-$retry_attempt"
    env NC_NO_ECM=1 \
        NC_LOGIN_SENT_FILE="$tmp/retry-login-sent-$retry_attempt" \
        NC_LOGIN_ACK_FILE="$retry_ack" \
        stdbuf -o0 -e0 "$tmp/ncclient" 127.0.0.1 "$newcamd_port" \
            u1 p1 0102030405060708091011121314 0 \
            > "$retry_client_out" 2>&1 &
    retry_pid=$!
    wait_client_stage5 "$retry_client_out" 100 || {
        echo "[FAIL] retry client did not receive a login result"; exit 1;
    }
    if wait "$retry_pid"; then retry_rc=0; else retry_rc=$?; fi
    retry_pid=
    if [ "$retry_rc" = 0 ] && [ -f "$retry_ack" ]; then
        retry_succeeded=1
        break
    fi
    sleep 0.05
done
[ "$retry_succeeded" = 1 ] || {
    echo "[FAIL] no follow-up real client received LOGIN_ACK after the poll timeout"; exit 1;
}
[ ! -e "$tmp/first-rst" ] || {
    echo "[FAIL] follow-up login succeeded only after the first peer sent RST"; exit 1;
}

if wait "$first_pid"; then first_rc=0; else first_rc=$?; fi
first_pid=
[ "$first_rc" = 0 ] || { echo "[FAIL] controlled first client exited $first_rc"; exit 1; }
[ "$retry_rc" = 0 ] || {
    sed 's/^/  /' "$retry_client_out"
    echo "[FAIL] follow-up client did not complete after LOGIN_ACK (exit $retry_rc)"; exit 1;
}
grep -q 'closed the socket with RST before LOGIN_ACK' "$tmp/first-client.out" || {
    echo "[FAIL] first client did not close with the controlled RST"; exit 1;
}
grep -q 'stage 5: LOGIN_ACK' "$retry_client_out" || {
    sed 's/^/  /' "$retry_client_out"
    echo "[FAIL] subsequent real login did not receive LOGIN_ACK"; exit 1;
}
grep -q 'stage 6 skipped (NC_NO_ECM)' "$retry_client_out" || {
    echo "[FAIL] follow-up client did not complete the no-ECM ACK check"; exit 1;
}
kill -0 "$server_pid" 2>/dev/null || {
    echo "[FAIL] server did not survive the controlled ACK backpressure case"; exit 1;
}

printf '%s\n' \
    "[ ok ] real TCP LOGIN_ACK send queue saturated to EAGAIN" \
    "[ ok ] /newcamd responded in ${page_time_ms} ms during live ACK poll #${poll_seq} (+${poll_offset_us} us)" \
    "[ ok ] duplicate login was rejected promptly while the first ACK was pending" \
    "[ ok ] production ACK send hit its 100 ms POLLOUT timeout under backpressure" \
    "[ ok ] a fresh login then received LOGIN_ACK before the first peer's RST" \
    "[ ok ] server remained alive"
