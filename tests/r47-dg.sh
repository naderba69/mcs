#!/bin/sh
# TASK R47 (D110): /diag and /cwlog on the CARD path, end to end.
#
# Run by `make -C tests dg` (target dg in stability.mk). Arguments:
#   $1  the stats binary      $2  the card fixture (ncserver)
#   $3  the newcamd client    (ncclient)
#
# A card (ncserver) feeds MultiCS, and a newcamd client asks for the same ECM
# twice. Two facts, each from a real server run:
#   A. the card answers in time: the channel reads delivered 2 on /diag, the
#      same two keys the client received. Deliveries are counted, but they
#      are not ring rows, so /cwlog stays empty of them by design.
#   B. the same card under DCW MINTIME 500: the answer (~60 ms) is refused as
#      too fast. /cwlog must name the peer "card" (not a number), /diag must
#      carry the reason card-too-fast, and both must agree.
#
# Ports 16870-16873. Exits 0 when every check passes, 1 otherwise.

BIN="$1"
NS="$2"
NC="$3"
HP=16870
TP=16871
SP=16872
CP=16873
fail=0

bad() { echo "[FAIL] $1"; fail=1; }
ok()  { echo "  [ ok ] $1"; }

run_case() {
    name="$1"
    extra="$2"
    cfg=".dg-$name.cfg"
    {
        printf 'HTTP PORT: %s\nHTTP USER: admin\nHTTP PASS: admin\n' "$HP"
        printf 'TELNET PORT: %s\nTELNET USER: admin\nTELNET PASS: admin\n' "$TP"
        printf 'DCW STATS: ON\n'
        printf 'N: 127.0.0.1 %s srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n' "$SP"
        printf '[ cardx ]\nCAID: 1884\nPORT: %s\n' "$CP"
        if [ -n "$extra" ]; then printf '%s\n' "$extra"; fi
        printf 'USER: u1 p1\n'
    } > "$cfg"

    rm -f ".dg-$name-"*.log ".dg-$name-"*.html
    NS_VERBOSE=1 stdbuf -o0 -e0 "$NS" "$SP" 0102030405060708091011121314 1884 000000 \
        > ".dg-$name-ns.log" 2>&1 &
    nsp=$!
    sleep 1
    stdbuf -o0 -e0 "$BIN" -C "$cfg" -v > ".dg-$name-srv.log" 2>&1 &
    srv=$!
    code=000
    i=0
    while [ $i -lt 30 ]; do
        code=$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HP/")
        [ "$code" = "200" ] && break
        sleep 1
        i=$((i + 1))
    done
    sleep 2
    "$NC" 127.0.0.1 "$CP" u1 p1 0102030405060708091011121314 0 1884 0064 \
        > ".dg-$name-cli.log" 2>&1
    sleep 2
    curl -s -m 5 -u admin:admin -o ".dg-$name-diag.html" "http://127.0.0.1:$HP/diag"
    curl -s -m 5 -u admin:admin -o ".dg-$name-cwlog.html" "http://127.0.0.1:$HP/cwlog"
    kill "$srv" "$nsp" 2>/dev/null
    sleep 1
    kill -9 "$srv" "$nsp" 2>/dev/null
    if [ "$code" != "200" ]; then
        bad "$name: the home page did not come up (http $code)"
        tail -4 ".dg-$name-srv.log"
    fi
}

echo "== R47 dg: case A, a card that answers in time =="
run_case a ""
got=$(grep -c "THE SERVER DELIVERED" .dg-a-cli.log)
if [ "$got" = "2" ]; then ok "the client received 2 control words"; else bad "client received $got of 2"; fi
del=$(grep -o '1884:000000:0064</td><td>[0-9]*</td>' .dg-a-diag.html | sed 's/.*<td>//; s/<.*//' | head -1)
if [ "$del" = "2" ]; then ok "/diag counts 2 deliveries on 1884:000000:0064 (the card path)";
else bad "/diag delivered for 1884:000000:0064 is '$del', want 2"; fi
if grep -q "Diagnosis" .dg-a-diag.html; then ok "/diag renders"; else bad "/diag did not render"; fi

echo "== R47 dg: case B, the same card refused as too fast =="
run_case b "DCW MINTIME: 500"
if grep -q "MINTIME" .dg-b-srv.log; then ok "the server refused the card's answer (MINTIME)"; else bad "no MINTIME refusal in the server log"; fi
if grep -q "<td>card</td>" .dg-b-cwlog.html && grep -q "card-too-fast" .dg-b-cwlog.html; then
    ok "/cwlog names the peer 'card' and the reason card-too-fast"
else bad "/cwlog does not show the card refusal"; fi
if grep -q "csp:65535" .dg-b-cwlog.html; then bad "/cwlog prints the card as csp:65535 (the old label)"; else ok "/cwlog has no csp:65535 label"; fi
if grep -q "1884:000000:0064</td><td>0</td><td>[1-9]" .dg-b-diag.html && grep -q "card-too-fast" .dg-b-diag.html; then
    ok "/diag refused the channel and gives the same reason"
else bad "/diag does not show the card refusal"; fi
if grep -q "<td>card</td><td>[1-9]" .dg-b-diag.html; then ok "/diag's peer table names the card";
else bad "/diag's peer table does not name the card"; fi

echo
if [ "$fail" = "0" ]; then echo "dg: ok (R47 card path, /diag and /cwlog)"; exit 0; fi
echo "dg: FAILED"
exit 1
