#!/bin/sh
# Ensure a peer stalled before LOGIN does not pin the config lifetime gate,
# and that its pre-reload handshake is rejected after the config generation
# changes. The test mutates only a throwaway config and observes HTTP output.
set -eu
cd "$(dirname "$0")"

BIN=${BIN:-../make-x64/x64/multics}
CC=${CC:-cc}
[ -x "$BIN" ] || { echo "build first: make -C ../make-x64"; exit 1; }
command -v curl >/dev/null || { echo "curl required"; exit 1; }
command -v stdbuf >/dev/null || { echo "stdbuf required"; exit 1; }
command -v python3 >/dev/null || { echo "python3 required"; exit 1; }

tmp=$(mktemp -d)
server_pid=
client_pid=
stop_pid() {
    pid=$1
    [ -n "$pid" ] || return 0
    kill "$pid" 2>/dev/null || true
    sleep 0.1
    if kill -0 "$pid" 2>/dev/null; then kill -9 "$pid" 2>/dev/null || true; fi
    wait "$pid" 2>/dev/null || true
}
cleanup() {
    stop_pid "$client_pid"
    stop_pid "$server_pid"
    rm -rf "$tmp"
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

$CC -O2 -m64 -std=gnu89 -w -o "$tmp/ncclient" \
    ncclient.c ../src/des.c ../src/msg-newcamd.c ../src/md5.c

cat > "$tmp/config.cfg" <<EOF
HTTP PORT: $http_port
HTTP USER: admin
HTTP PASS: admin
HTTP TITLE: "Newcamd Pin Initial"

[ pinprobe ]
CAID: 1884
PORT: $newcamd_port
USER: u1 p1
EOF

"$BIN" -C "$tmp/config.cfg" > "$tmp/server.out" 2>&1 &
server_pid=$!

code=000
i=0
while [ "$i" -lt 100 ]; do
    code=$(curl -s -m 0.5 -u admin:admin -o "$tmp/root.html" -w '%{http_code}' \
        "http://127.0.0.1:$http_port/" 2>/dev/null || true)
    [ "$code" = 200 ] && grep -q 'Newcamd Pin Initial' "$tmp/root.html" && break
    kill -0 "$server_pid" 2>/dev/null || break
    sleep 0.05
    i=$((i + 1))
done
[ "$code" = 200 ] && grep -q 'Newcamd Pin Initial' "$tmp/root.html" || {
    echo "[FAIL] test server did not publish its initial title (HTTP $code)"; exit 1;
}

env NC_KEYMOD_READY_FILE="$tmp/key-ready" \
    NC_WAIT_BEFORE_LOGIN_FILE="$tmp/login-release" \
    NC_LOGIN_SENT_FILE="$tmp/login-sent" \
    NC_NO_ECM=1 \
    stdbuf -o0 -e0 "$tmp/ncclient" 127.0.0.1 "$newcamd_port" \
        u1 p1 0102030405060708091011121314 0 \
        > "$tmp/client.out" 2>&1 &
client_pid=$!

wait_file() {
    path=$1
    limit=${2:-100}
    count=0
    while [ "$count" -lt "$limit" ]; do
        [ -f "$path" ] && return 0
        kill -0 "$server_pid" 2>/dev/null || return 1
        sleep 0.05
        count=$((count + 1))
    done
    return 1
}
wait_file "$tmp/key-ready" 100 || {
    echo "[FAIL] client did not reach the pre-login barrier"; exit 1;
}
# The HTTP listener can answer during the final startup steps; let the config
# thread install its inotify watch before provoking a close-write event.
sleep 0.5

python3 - "$tmp/config.cfg" <<'PY'
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = 'HTTP TITLE: "Newcamd Pin Initial"'
new = 'HTTP TITLE: "Newcamd Pin Reloaded"'
if text.count(old) != 1:
    raise SystemExit("expected one initial test title")
path.write_text(text.replace(old, new), encoding="utf-8")
PY

# Require the reload to publish through the authenticated web surface while
# the client still withholds LOGIN. A worker retaining the old 3 s socket-read
# pin makes this 2.5 s gate fail instead of being mistaken for a successful
# reload that happened after the receive timeout.
python3 - "$http_port" <<'PY'
import base64
import http.client
import re
import sys
import time

port = int(sys.argv[1])
end = time.monotonic() + 2.5
auth = "Basic " + base64.b64encode(b"admin:admin").decode("ascii")
last_status = None
last_initial_title = False
last_title = None
last_error = None
while time.monotonic() < end:
    connection = None
    try:
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=0.20)
        connection.request("GET", "/", headers={"Authorization": auth})
        response = connection.getresponse()
        body = response.read()
        last_status = response.status
        title = re.search(rb"<title>(.*?)</title>", body, re.I | re.S)
        last_title = title.group(1).decode("utf-8", "replace") if title else None
        last_initial_title = b"Newcamd Pin Initial" in body
        if response.status == 200 and b"Newcamd Pin Reloaded" in body:
            elapsed_ms = (2.5 - max(0.0, end - time.monotonic())) * 1000.0
            print("[ ok ] config reload reached authenticated HTTP page in %.1f ms" % elapsed_ms)
            sys.exit(0)
    except Exception as error:
        last_error = type(error).__name__
    finally:
        if connection is not None:
            connection.close()
    time.sleep(0.025)
print("[FAIL] config reload did not publish before the slow-login deadline; "
      "last HTTP status=%r title=%r initial-title=%s error=%s" %
      (last_status, last_title, last_initial_title, last_error))
sys.exit(1)
PY

touch "$tmp/login-release"
wait_file "$tmp/login-sent" 100 || {
    echo "[FAIL] controlled client did not send its pre-reload login"; exit 1;
}
i=0
while [ "$i" -lt 100 ]; do
    grep -Eq 'stage 5: (no usable login answer|LOGIN_ACK|LOGIN_NAK|unexpected reply)' \
        "$tmp/client.out" 2>/dev/null && break
    kill -0 "$server_pid" 2>/dev/null || { echo "[FAIL] server exited during reload test"; exit 1; }
    sleep 0.05
    i=$((i + 1))
done
grep -q 'stage 5: no usable login answer' "$tmp/client.out" || {
    sed 's/^/  /' "$tmp/client.out"
    echo "[FAIL] a handshake accepted before reload was not rejected afterward"; exit 1;
}
if wait "$client_pid"; then client_rc=0; else client_rc=$?; fi
client_pid=
[ "$client_rc" -ne 0 ] || {
    echo "[FAIL] stale pre-reload client unexpectedly completed login"; exit 1;
}
kill -0 "$server_pid" 2>/dev/null || {
    echo "[FAIL] server did not survive config reload"; exit 1;
}

printf '%s\n' \
    "[ ok ] a stalled pre-login receive did not pin configuration reload" \
    "[ ok ] the old-generation login was rejected after reload" \
    "[ ok ] server remained alive"
