#!/bin/sh
# TASK R48 (D111): the detail scope, end to end, switched ON WHILE THE SERVER RUNS.
#
# Run by `make -C tests dg` (target dg in stability.mk). Arguments:
#   $1  the stats binary      $2  the card fixture (ncserver)
#   $3  the newcamd client    (ncclient)
#
# The scope is turned on and off with the telnet command `diagscope`, with no
# restart and no config line. Facts checked, each on a real server run:
#   A. outside the scope a channel is counted but not timed (card answers 0).
#      `diagscope add profile cardx` turns the timing on for that profile, live.
#      The next requests are timed (card answers >= 1), /diag and the TXT export
#      show it, /cwlog?ch= filters to the channel, and the telnet list agrees.
#   B. a card that answers after DCW TIMEOUT: the request is counted as no
#      answer, but only for a channel in the scope (added live by channel).
#
# Ports 16880-16883. Exits 0 when every check passes, 1 otherwise.

BIN="$1"
NS="$2"
NC="$3"
HP=16880
TP=16881
SP=16882
CP=16883
CH=1884:000000:0064
fail=0
srv=""
nsp=""

bad() { echo "[FAIL] $1"; fail=1; }
ok()  { echo "  [ ok ] $1"; }

tn() { python3 r48-tn.py "$TP" admin admin "$@"; }

# TXT export: one line per channel, fields separated by ';'.
# field 6 = no_answer, field 7 = card_answers, field 2 = delivered.
txt_field() {
    curl -s -m 5 -u admin:admin -o ".ds-$1-txt.csv" "http://127.0.0.1:$HP/diag?format=txt"
    awk -F';' -v c="$CH" -v f="$2" '$1==c { print $f; exit }' ".ds-$1-txt.csv"
}

start_srv() {
    name="$1"
    extra="$2"
    delays="$3"
    cfg=".ds-$name.cfg"
    {
        printf 'HTTP PORT: %s\nHTTP USER: admin\nHTTP PASS: admin\n' "$HP"
        printf 'TELNET PORT: %s\nTELNET USER: admin\nTELNET PASS: admin\n' "$TP"
        printf 'DCW STATS: ON\n'
        printf 'N: 127.0.0.1 %s srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n' "$SP"
        printf '[ cardx ]\nCAID: 1884\nPORT: %s\n' "$CP"
        if [ -n "$extra" ]; then printf '%s\n' "$extra"; fi
        # two users: a newcamd server refuses a reconnect of the same user within
        # 60 s (stock rule), so the second request uses u2.
        printf 'USER: u1 p1\nUSER: u2 p2\n'
    } > "$cfg"

    rm -f ".ds-$name-"*.log ".ds-$name-"*.csv
    NS_DELAYS="$delays" stdbuf -o0 -e0 "$NS" "$SP" 0102030405060708091011121314 1884 000000 \
        > ".ds-$name-ns.log" 2>&1 &
    nsp=$!
    sleep 1
    stdbuf -o0 -e0 "$BIN" -C "$cfg" -v > ".ds-$name-srv.log" 2>&1 &
    srv=$!
    code=000
    i=0
    while [ $i -lt 30 ]; do
        code=$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HP/")
        [ "$code" = "200" ] && break
        sleep 1
        i=$((i + 1))
    done
    sleep 1
    if [ "$code" != "200" ]; then
        bad "$name: the home page did not come up (http $code)"
        tail -4 ".ds-$name-srv.log"
    fi
}

stop_srv() {
    kill "$srv" "$nsp" 2>/dev/null
    sleep 1
    kill -9 "$srv" "$nsp" 2>/dev/null
    sleep 1
}

echo "== R48 scope, case A: timing switched on for one profile, live =="
start_srv a "" 60

out=$(tn 'diagscope')
if echo "$out" | grep -q "detail is OFF"; then ok "a fresh server starts with the detail OFF"; else bad "detail is not OFF at start: $out"; fi

# One request only: a client that re-asks the same ECM marks the source to avoid
# for the channel (the existing anti-black-screen rule), which would hide the card.
NC_ECMS=1 "$NC" 127.0.0.1 "$CP" u1 p1 0102030405060708091011121314 0 1884 0064 > ".ds-a-cli1.log" 2>&1
sleep 2
lat=$(txt_field a 7)
if [ "$lat" = "0" ]; then ok "outside the scope the card answers are counted as 0 (not timed)"; else bad "card answers outside the scope: '$lat', want 0"; fi
del=$(txt_field a 2)
if [ "$del" = "1" ]; then ok "the delivery is still counted outside the scope (1)"; else bad "delivered outside the scope: '$del', want 1"; fi

out=$(tn 'diagscope add profile cardx' 'diagscope')
if echo "$out" | grep -q "profile cardx added"; then ok "the profile is added on the live server"; else bad "add profile failed: $out"; fi
if echo "$out" | grep -q "detail is ON"; then ok "the detail reads ON right after the change"; else bad "detail is not ON after the change"; fi
if echo "$out" | grep -q "profile cardx"; then ok "the telnet list names the profile"; else bad "the telnet list does not name the profile"; fi

# NC_FILL=7 changes the ECM body, so this is a new request, not a cache hit.
NC_FILL=7 "$NC" 127.0.0.1 "$CP" u2 p2 0102030405060708091011121314 0 1884 0064 > ".ds-a-cli2.log" 2>&1
sleep 2
lat=$(txt_field a 7)
if [ -n "$lat" ] && [ "$lat" -ge 1 ] 2>/dev/null; then ok "after the change the card answers are timed ($lat)"; else bad "card answers after the change: '$lat', want >= 1"; fi
curl -s -m 5 -u admin:admin -o ".ds-a-diag.html" "http://127.0.0.1:$HP/diag"
if grep -qF "Detail scope:" ".ds-a-diag.html" && grep -qF "/cwlog?ch=$CH" ".ds-a-diag.html"; then
    ok "/diag shows the detail scope and links the channel to its /cwlog rows"
else bad "/diag does not show the detail scope or the channel link"; fi
curl -s -m 5 -u admin:admin -o ".ds-a-cwch.html" -w '%{http_code}' "http://127.0.0.1:$HP/cwlog?ch=$CH" > ".ds-a-cwch.code"
if [ "$(cat .ds-a-cwch.code)" = "200" ] && grep -qF "show all" ".ds-a-cwch.html"; then
    ok "/cwlog?ch= filters to the channel and offers 'show all'"
else bad "/cwlog?ch= did not filter as designed"; fi
ct=$(curl -s -m 5 -u admin:admin -D - -o /dev/null "http://127.0.0.1:$HP/diag?format=txt" | tr -d '\r' | grep -i '^content-disposition:' )
if echo "$ct" | grep -qi 'attachment; filename="multics-diag.txt"'; then ok "the TXT export is an attachment (multics-diag.txt)"; else bad "TXT export headers: '$ct'"; fi

out=$(tn 'diagscope del profile cardx' 'diagscope add channel 1884:000000:0064' 'diagscope add channel 1884:000000:0064' 'diagscope add channel zz' 'diagscope clear' 'diagscope')
if echo "$out" | grep -q "profile cardx removed"; then ok "the profile is removed live"; else bad "del profile failed"; fi
if echo "$out" | grep -q "channel 1884:000000:0064 added"; then ok "the channel is added live"; else bad "add channel failed"; fi
if echo "$out" | grep -q "already in scope"; then ok "adding the same channel twice says so"; else bad "duplicate add not reported"; fi
if echo "$out" | grep -q "a channel is CAID:PROV:SID in hex"; then ok "a bad channel is refused with the format"; else bad "bad channel not refused"; fi
if echo "$out" | grep -q "cleared, detail is OFF"; then ok "clear turns the detail OFF"; else bad "clear failed"; fi
stop_srv

echo "== R48 scope, case B: a card past DCW TIMEOUT, scoped by channel =="
start_srv b "DCW TIMEOUT: 2000" "6000"
out=$(tn 'diagscope add channel 1884:000000:0064')
if echo "$out" | grep -q "channel 1884:000000:0064 added"; then ok "the channel is in the scope before the request"; else bad "channel not added: $out"; fi
NC_ECMS=1 timeout 25 "$NC" 127.0.0.1 "$CP" u1 p1 0102030405060708091011121314 0 1884 0064 > ".ds-b-cli.log" 2>&1
sleep 4
na=$(txt_field b 6)
if [ -n "$na" ] && [ "$na" -ge 1 ] 2>/dev/null; then ok "the request with no answer in time is counted ($na)"; else bad "no answer counted: '$na', want >= 1"; fi
curl -s -m 5 -u admin:admin -o ".ds-b-diag.html" "http://127.0.0.1:$HP/diag"
if grep -qF "no answer" ".ds-b-diag.html"; then ok "/diag has the no-answer column"; else bad "/diag has no no-answer column"; fi
stop_srv

echo
if [ "$fail" = "0" ]; then echo "scope: ok (R48 live detail scope, no restart)"; exit 0; fi
echo "scope: FAILED"
exit 1
