#!/usr/bin/env bash
# Live peer-reputation regression exercised only through the existing /json
# and /cache web readouts. No server, peer, or client logs are inspected.

set -u

BIN=${BIN:-../make-x64/x64/multics}
NCCLIENT=${NCCLIENT:-./.ncclient.bin}
CACHEPEER=${CACHEPEER:-./.cachepeer.bin}
WEB=./peerrep-web.py
PR_HPORT=16920
PR_CACHE=16922
PR_A=16923
PR_B=16924
PR_NPORT=16925
PR_CFG=.pr-srv.cfg
PR_REP=.pr.rep
PR_KEY=0102030405060708091011121314
PR_CW=10203060405060F070809080A0B0C010

srv=
pa=
pb=
cli=
ok=1

pass() { printf '  [ ok ] %s\n' "$*"; }
fail() { printf '[FAIL] %s\n' "$*" >&2; ok=0; }

stop_pid() {
    local pid=${1:-}
    [ -n "$pid" ] || return 0
    kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 1 20); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

cleanup() {
    stop_pid "${cli:-}"
    stop_pid "${pa:-}"
    stop_pid "${pb:-}"
    stop_pid "${srv:-}"
    pkill -9 -x multics 2>/dev/null || true
    pkill -9 -x .cachepeer.bin 2>/dev/null || true
    rm -f "$PR_CFG" multics.peers
    if [ "$ok" = 1 ]; then
        rm -f "$PR_REP" .pr.json
    else
        printf '[INFO] Last web snapshot retained at tests/.pr.json\n' >&2
    fi
}
trap cleanup EXIT INT TERM

fail_now() {
    fail "$*"
    exit 1
}

if [ ! -x "$BIN" ]; then
    fail_now "build first: make -C ../make-x64"
fi
if [ ! -x "$NCCLIENT" ] || [ ! -x "$CACHEPEER" ]; then
    fail_now "peer/client fixtures were not built by the pr target dependencies"
fi

# A stale instance from a previously interrupted run can otherwise own these
# fixed test ports. No production configuration or web setting is changed.
pkill -9 -x multics 2>/dev/null || true
pkill -9 -x .cachepeer.bin 2>/dev/null || true
sleep 1
rm -f "$PR_REP" "$PR_CFG" .pr.json multics.peers

cat > "$PR_CFG" <<EOF
HTTP PORT: $PR_HPORT
HTTP USER: a
HTTP PASS: a
HTTP TITLE: mcs-pr
TELNET PORT: 16921
TELNET USER: a
TELNET PASS: a
DCW STATS: ON
CACHE PORT: $PR_CACHE
CACHE FILTER: OFF
PEER REPUTATION: ON
PEER REPUTATION FILE: $PR_REP
PEER REPUTATION DISTRUST: 3
PEER REPUTATION ISOLATE: 6
PEER REPUTATION BAN: 10

[ pr ]
CAID: 1884
PROVIDERS: 0
SID LIST: 0064.81 0065.81 0066.81 0067.81
PORT: $PR_NPORT
CACHE TIMEOUT: 2000
DCW TIMEOUT: 4000
DCWFILTER CYCLE: YES
USER: u1 p1

CACHE PEER: 127.0.0.1:$PR_A { csp=1; fwd=1 }
CACHE PEER: 127.0.0.1:$PR_B { csp=1; fwd=1 }
EOF

start_server() {
    "$BIN" -C "$PR_CFG" -v >/dev/null 2>&1 &
    srv=$!
    if ! python3 "$WEB" json-ready "$PR_HPORT" 30; then
        fail "server /json did not become ready"
        return 1
    fi
    return 0
}

start_peer_a() {
    env CP_CYCLE_MARK=1 CP_ANSWER_DELAY_MS=800 \
        "$CACHEPEER" "$PR_CACHE" "$PR_A" "$PR_CW" 64 >/dev/null 2>&1 &
    pa=$!
    if ! python3 "$WEB" online "$PR_HPORT" "$PR_A" 40; then
        fail "peer A did not appear online with cards in /json and /cache"
        return 1
    fi
    return 0
}

start_peer_b() {
    local short_reply=${1:-0}
    if [ "$short_reply" = 1 ]; then
        env CP_CYCLE_MARK=2 CP_SHORT_REPLY=1 "$CACHEPEER" "$PR_CACHE" "$PR_B" "$PR_CW" 64 >/dev/null 2>&1 &
    else
        env CP_CYCLE_MARK=2 CP_REPUSH_MS=250 "$CACHEPEER" "$PR_CACHE" "$PR_B" "$PR_CW" 64 >/dev/null 2>&1 &
    fi
    pb=$!
    if ! python3 "$WEB" online "$PR_HPORT" "$PR_B" 55; then
        fail "peer B did not appear online with cards in /json and /cache"
        return 1
    fi
    return 0
}

stop_generation() {
    stop_pid "${cli:-}"; cli=
    stop_pid "${pa:-}"; pa=
    stop_pid "${pb:-}"; pb=
    stop_pid "${srv:-}"; srv=
    sleep 1
}

web_sent() { python3 "$WEB" sent "$PR_HPORT" "$1"; }
web_replies() { python3 "$WEB" replies "$PR_HPORT" "$1"; }
web_hits() { python3 "$WEB" hits "$PR_HPORT" "$1"; }

run_client() {
    local sid=$1
    local require_cw=${2:-1}
    if [ "$require_cw" = 1 ]; then
        if ! env NC_ECMS=1 NC_REQUIRE_CW=1 "$NCCLIENT" \
            127.0.0.1 "$PR_NPORT" u1 p1 "$PR_KEY" 0 1884 "$sid" \
            >/dev/null 2>&1; then
            fail "Newcamd client did not receive a CW for SID $sid"
            return 1
        fi
    else
        if ! env NC_ECMS=1 "$NCCLIENT" \
            127.0.0.1 "$PR_NPORT" u1 p1 "$PR_KEY" 0 1884 "$sid" \
            >/dev/null 2>&1; then
            fail "Newcamd client transport failed for probe SID $sid"
            return 1
        fi
    fi
    return 0
}

if ! start_server; then fail_now "phase 1 setup failed"; fi
if ! start_peer_a; then fail_now "phase 1 peer readiness failed"; fi

a_sent_before=$(web_sent "$PR_A") || fail_now "cannot read A Sent REQ from /cache"
a_replies_before=$(web_replies "$PR_A") || fail_now "cannot read A reply count from /json"
a_hits_before=$(web_hits "$PR_A") || fail_now "cannot read A hit count from /json"

env NC_ECMS=1 NC_REQUIRE_CW=1 "$NCCLIENT" \
    127.0.0.1 "$PR_NPORT" u1 p1 "$PR_KEY" 0 1884 64 \
    >/dev/null 2>&1 &
cli=$!
if ! python3 "$WEB" pending "$PR_HPORT" "$PR_A" \
        "$a_sent_before" "$a_replies_before" 12; then
    fail "phase 1 did not prove an outstanding ECM via /cache Sent REQ and /json before the reply"
    fail_now "pending-ECM evidence is a required gate"
fi
if ! kill -0 "$cli" 2>/dev/null; then
    fail_now "client was no longer working while pending-ECM state was observed"
fi
if ! wait "$cli"; then
    cli=
    fail_now "phase 1 client failed to receive the cache CW"
fi
cli=
if ! python3 "$WEB" served "$PR_HPORT" "$PR_A" \
        "$a_replies_before" "$a_hits_before" 12; then
    fail_now "phase 1 /json did not show the accepted cache reply and hit"
fi
pass "phase 1: /cache Sent REQ increased while /json reply count stayed unchanged and the client remained active; the real CW then arrived"

if ! start_peer_b 0; then fail_now "phase 2 peer readiness failed"; fi
b_sent_before=$(web_sent "$PR_B") || fail_now "cannot read B Sent REQ from /cache"
if ! run_client 65 0; then fail_now "phase 2 could not send an ECM probe to the cache mesh"; fi
if ! python3 "$WEB" wait-sent "$PR_HPORT" "$PR_B" "$b_sent_before" 12; then
    fail_now "phase 2 /cache did not show B receiving a cache request"
fi
if ! python3 "$WEB" stage "$PR_HPORT" "$PR_B" 1 3 - 25; then
    fail_now "phase 2 /json did not show B's reputation reaching distrust after confirmed conflicts"
fi
pass "phase 2: /json shows the threshold-crossing reputation evidence"

# The repeated marked pushes continue without more client requests. Wait for
# the existing web readout to show the configured temporary isolate stage.
if ! python3 "$WEB" stage "$PR_HPORT" "$PR_B" 2 6 push-while-isolated 25; then
    fail_now "phase 3 /json did not show the bounded isolate and diagnostic-only later pushes"
fi
areq_before=$(web_sent "$PR_A") || fail_now "cannot read A Sent REQ before isolation check"
breq_before=$(web_sent "$PR_B") || fail_now "cannot read B Sent REQ before isolation check"
a_replies_before=$(web_replies "$PR_A") || fail_now "cannot read A reply count before isolation check"
if ! run_client 67 0; then fail_now "phase 3 ECM probe could not be sent"; fi
if ! python3 "$WEB" wait-sent "$PR_HPORT" "$PR_A" "$areq_before" 12; then
    fail_now "phase 3 /cache did not show A receiving the isolated-channel request"
fi
areq_after=$(web_sent "$PR_A") || fail_now "cannot read A Sent REQ after isolation check"
breq_after=$(web_sent "$PR_B") || fail_now "cannot read B Sent REQ after isolation check"
if [ "$breq_after" != "$breq_before" ]; then
    fail_now "phase 3 /cache shows isolated B was still asked ($breq_before->$breq_after)"
fi
if ! python3 "$WEB" wait-reply "$PR_HPORT" "$PR_A" "$a_replies_before" 12; then
    fail_now "phase 3 /json did not show A's cache reply"
fi
if ! python3 "$WEB" stage "$PR_HPORT" "$PR_B" 2 6 push-while-isolated 5; then
    fail_now "phase 3 /json did not retain the bounded isolate stage"
fi
pass "phase 3: /cache shows A's Sent REQ rising and isolated B unchanged; /json retains the bounded isolate"

stop_generation
if ! start_server; then fail_now "phase 4 server restart failed"; fi
if ! start_peer_a || ! start_peer_b 0; then fail_now "phase 4 peer readiness failed"; fi
if ! python3 "$WEB" history "$PR_HPORT" "$PR_B" present 6; then
    fail_now "phase 4 /json did not show persisted history at stage 0 without restoring active quarantine"
fi
b_sent_before=$(web_sent "$PR_B") || fail_now "cannot read B Sent REQ after restart"
if ! run_client 64; then fail_now "phase 4 mesh did not serve after restart"; fi
if ! python3 "$WEB" wait-sent "$PR_HPORT" "$PR_B" "$b_sent_before" 12; then
    fail_now "phase 4 /cache did not show B askable after history-only restart"
fi
pass "phase 4: /json shows persisted history only; /cache confirms B is askable after restart"

stop_generation
if [ ! -f "$PR_REP" ]; then
    fail_now "phase 5 cannot clear peer history because the persistence file was not created"
fi
python3 - "$PR_REP" "$PR_B" <<'PY'
import os
import sys

path, port = sys.argv[1:]
needle = "127.0.0.1:%s " % port
with open(path, "r", encoding="utf-8") as source:
    rows = source.readlines()
kept = [row for row in rows if not row.startswith(needle)]
tmp = path + ".new"
with open(tmp, "w", encoding="utf-8") as target:
    target.writelines(kept)
os.replace(tmp, path)
PY
if [ "$?" -ne 0 ]; then fail_now "operator history-row removal failed"; fi

if ! start_server; then fail_now "phase 5 server restart failed"; fi
if ! start_peer_a || ! start_peer_b 1; then fail_now "phase 5 peer readiness failed"; fi
if ! python3 "$WEB" history "$PR_HPORT" "$PR_B" absent; then
    fail_now "phase 5 /json did not show the operator-cleared history as absent"
fi
b_sent_before=$(web_sent "$PR_B") || fail_now "cannot read B Sent REQ after history clear"
if ! run_client 64; then fail_now "phase 5 mesh failed after the history clear"; fi
if ! python3 "$WEB" wait-sent "$PR_HPORT" "$PR_B" "$b_sent_before" 12; then
    fail_now "phase 5 /cache did not show the cleared peer askable again"
fi
pass "phase 5: /json confirms the cleared history is absent; /cache confirms B can be asked again"

exit "$((1 - ok))"
