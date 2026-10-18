#!/bin/sh
# TASK R65: the rebuilt /diag page, end to end on a real server run.
#
# Run by `make -C tests dg` (target dg in stability.mk, after r48-scope.sh).
# Arguments:
#   $1  the stats binary      $2  the card fixture (ncserver)
#   $3  the newcamd client    (ncclient)
#
# A card feeds MultiCS and a client asks for the same ECM twice, so the channel
# 1884:000000:0064 has two deliveries. Then, each checked on the live server:
#   A. the page renders in full (ends with </html>), with the summary, the
#      findings, the filter form, the channel row, the profile table and the
#      legend.
#   B. the drill-down (/diag?ch=) shows the channel alone: its counters, its
#      profiles and its ring rows; an unknown channel says so, without an error.
#   C. the filter and sort parameters are honoured; a bad CAID is ignored.
#   D. the TXT export has the 19 columns on its header and on every data row,
#      and the channel row carries the two deliveries.
#   E. counting OFF and ON from the page; a cross-site request changes nothing
#      (403); the counters reset clears the channel table.
#   F. /json still answers (the machine readout is unchanged).
#
# Ports 16890-16893. Exits 0 when every check passes, 1 otherwise.

BIN="$1"
NS="$2"
NC="$3"
HP=16890
TP=16891
SP=16892
CP=16893
CH=1884:000000:0064
fail=0
srv=""
nsp=""

bad() { echo "[FAIL] $1"; fail=1; }
ok()  { echo "  [ ok ] $1"; }
web() { curl -s -m 5 -u admin:admin "http://127.0.0.1:$HP$1"; }
code() { curl -s -m 5 -u admin:admin -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HP$1"; }

cfg=".r65-diag.cfg"
{
    printf 'HTTP PORT: %s\nHTTP USER: admin\nHTTP PASS: admin\n' "$HP"
    printf 'TELNET PORT: %s\nTELNET USER: admin\nTELNET PASS: admin\n' "$TP"
    printf 'DCW STATS: ON\n'
    printf 'N: 127.0.0.1 %s srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n' "$SP"
    printf '[ cardx ]\nCAID: 1884\nPORT: %s\n' "$CP"
    printf 'USER: u1 p1\n'
} > "$cfg"

cleanup() {
    kill "$srv" "$nsp" 2>/dev/null
    sleep 1
    kill -9 "$srv" "$nsp" 2>/dev/null
}

rm -f .r65-*.html .r65-*.txt .r65-*.log
NS_VERBOSE=1 stdbuf -o0 -e0 "$NS" "$SP" 0102030405060708091011121314 1884 000000 \
    > ".r65-ns.log" 2>&1 &
nsp=$!
sleep 1
stdbuf -o0 -e0 "$BIN" -C "$cfg" -v > ".r65-srv.log" 2>&1 &
srv=$!
c=000
i=0
while [ $i -lt 30 ]; do
    c=$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HP/")
    [ "$c" = "200" ] && break
    sleep 1
    i=$((i + 1))
done
if [ "$c" != "200" ]; then
    bad "the home page did not come up (http $c)"
    tail -4 ".r65-srv.log"
    cleanup
    exit 1
fi
sleep 2
"$NC" 127.0.0.1 "$CP" u1 p1 0102030405060708091011121314 0 1884 0064 > ".r65-cli.log" 2>&1
sleep 2

# ---- A. the full page ----
web /diag > .r65-page.html
if tail -c 40 .r65-page.html | grep -q "</html>"; then ok "A: /diag renders to the end (</html>)"; else bad "A: /diag is cut short"; fi
for want in "Findings: what the evidence says" "Channels" "Packages (CAID)" "Configured profiles: effective settings" \
            "Duplicate CAID:provider pairs" "Legend: what each number and reason means" "name=sort" "name=show" \
            "DCWFILTER CYCLE" "/profile?id=" "counters since"; do
    if grep -q "$want" .r65-page.html; then ok "A: page has '$want'"; else bad "A: page lacks '$want'"; fi
done
# the profile row names its warnings; the legend below is static and proves nothing
profwarn=$(grep "href='/profile?id=" .r65-page.html | grep "DCWFILTER CYCLE" | head -1)
if grep -q "inherit (global OFF)" .r65-page.html && [ -n "$profwarn" ]; then ok "A: a profile on INHERIT with the global CYCLE off reads so and warns"; else bad "A: the CYCLE readout or warning is wrong"; fi
if grep -q "1884:000000:0064" .r65-page.html; then ok "A: the channel row is on the page"; else bad "A: the channel row is missing"; fi
if grep -q "href='/diag?ch=1884:000000:0064'" .r65-page.html; then ok "A: the channel has a details link"; else bad "A: no details link"; fi

# ---- B. the drill-down ----
web "/diag?ch=$CH" > .r65-ch.html
if grep -q "Last verdicts of this channel" .r65-ch.html; then ok "B: the drill-down renders"; else bad "B: the drill-down is missing"; fi
if grep -q "Profiles that serve this CAID" .r65-ch.html && grep -q "cardx" .r65-ch.html; then ok "B: the drill-down lists the profile that serves 1884"; else bad "B: the profile list is missing"; fi
if tail -c 40 .r65-ch.html | grep -q "</html>"; then ok "B: the drill-down ends with </html>"; else bad "B: the drill-down is cut short"; fi
web "/diag?ch=FFFF:000000:0001" > .r65-none.html
if grep -q "not in the table" .r65-none.html; then ok "B: an unknown channel says so"; else bad "B: unknown channel not reported"; fi
web "/diag?ch=zz" > .r65-bad.html
if [ -s .r65-bad.html ] && grep -q "Channels" .r65-bad.html; then ok "B: a bad ch value falls back to the page"; else bad "B: a bad ch value broke the page"; fi

# ---- C. filters ----
web "/diag?show=dying&sort=refused&caid=1884" > .r65-f1.html
if [ "$(code "/diag?show=problems&sort=slow")" = "200" ]; then ok "C: show=problems&sort=slow answers"; else bad "C: a valid filter failed"; fi
if [ "$(code "/diag?sort=bogus&show=nope&caid=zz")" = "200" ]; then ok "C: unknown sort, show and caid are ignored (200)"; else bad "C: a bad filter broke the page"; fi
if grep -q "1884:000000:0064" .r65-f1.html; then bad "C: show=dying still lists a channel that does not fail"; else ok "C: show=dying hides a channel that is delivering"; fi

# ---- D. TXT export ----
web "/diag?format=txt" > .r65-export.txt
hdr=$(grep -m1 '^channel;' .r65-export.txt)
nf=$(echo "$hdr" | awk -F';' '{print NF}')
if [ "$nf" = "19" ]; then ok "D: the export header has 19 columns"; else bad "D: the export header has $nf columns, want 19"; fi
echo "$hdr" | grep -q "ca_system;cw_format;checksum_gate;delivered_pct;refused_1m;refused_5m;last_ring_age_s" \
    && ok "D: the R65 columns are appended after 'setting'" || bad "D: the R65 columns are missing"
badrows=$(grep -v '^#' .r65-export.txt | grep -v '^channel;' | awk -F';' 'NF!=19 && NF>0 {n++} END {print n+0}')
if [ "$badrows" = "0" ]; then ok "D: every data row has 19 fields"; else bad "D: $badrows data rows have a wrong field count"; fi
row=$(grep "^1884:000000:0064;" .r65-export.txt)
if echo "$row" | awk -F';' '$2=="2"{f=1} END{exit !f}'; then ok "D: the channel shows 2 deliveries in the export"; else bad "D: the export row is wrong: $row"; fi
if grep -v '^#' .r65-export.txt | grep -q "srv1\|peer"; then bad "D: a data row names a peer"; else ok "D: no data row names a peer (the header comment says so)"; fi

# ---- E. actions ----
web "/diag/counting?op=off" > /dev/null
if web /diag | grep -q "counting: turn ON\|>OFF<"; then ok "E: counting OFF from the page"; else bad "E: counting did not turn OFF"; fi
web "/diag/counting?op=on" > /dev/null
if web /diag | grep -q "counting: turn OFF"; then ok "E: counting ON again from the page"; else bad "E: counting did not turn ON"; fi
c403=$(curl -s -m 5 -u admin:admin -H 'Sec-Fetch-Site: cross-site' -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HP/diag/counting?op=off")
if [ "$c403" = "403" ] && web /diag | grep -q "counting: turn OFF"; then ok "E: a cross-site counting change is refused and changes nothing"; else bad "E: cross-site counting change not refused (http $c403)"; fi
c403=$(curl -s -m 5 -u admin:admin -H 'Sec-Fetch-Site: cross-site' -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HP/diag/reset")
if [ "$c403" = "403" ]; then ok "E: a cross-site reset is refused"; else bad "E: cross-site reset not refused (http $c403)"; fi
web /diag/reset > /dev/null
web "/diag?format=txt" > .r65-after.txt
if grep -q "^1884:000000:0064;" .r65-after.txt; then bad "E: the reset did not clear the channel table"; else ok "E: the reset clears the channel table"; fi

# ---- F. the machine readout ----
if [ "$(code /json)" = "200" ]; then ok "F: /json answers"; else bad "F: /json does not answer"; fi

cleanup

# ---- G. the global CYCLE gate: a profile on INHERIT follows it ----
# A second, card-less server with DCWFILTER CYCLE: YES in the general section.
# The profile must read as following the global gate, and must not warn.
cfgg=".r65-cycle.cfg"
{
    printf 'HTTP PORT: 16894\nHTTP USER: admin\nHTTP PASS: admin\n'
    printf 'TELNET PORT: 16895\nTELNET USER: admin\nTELNET PASS: admin\n'
    printf 'DCW STATS: ON\nDCWFILTER CYCLE: YES\n'
    printf '[ cx<i>&</i> ]\nCAID: 1884\nPORT: 16896\nUSER: u1 p1\n'
} > "$cfgg"
stdbuf -o0 -e0 "$BIN" -C "$cfgg" -v > ".r65-cyc-srv.log" 2>&1 &
srv=$!
c=000
i=0
while [ $i -lt 30 ]; do
    c=$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' "http://127.0.0.1:16894/")
    [ "$c" = "200" ] && break
    sleep 1
    i=$((i + 1))
done
curl -s -m 5 -u admin:admin "http://127.0.0.1:16894/diag" > .r65-cyc.html
if grep -q "cx&lt;i&gt;&amp;" .r65-cyc.html && ! grep -q "cx<i>&" .r65-cyc.html; then ok "G: a profile name with HTML characters is escaped on the page"; else bad "G: a profile name is not escaped"; fi
if grep -q "inherit (global ON)" .r65-cyc.html; then ok "G: a profile on INHERIT reads 'inherit (global ON)'"; else bad "G: the profile does not read the global CYCLE gate (http $c)"; fi
profwarn=$(grep "href='/profile?id=" .r65-cyc.html | grep "DCWFILTER CYCLE" | head -1)
if [ -z "$(grep "href='/profile?id=" .r65-cyc.html)" ]; then bad "G: the profile row was not found (the check would prove nothing)"; elif [ -n "$profwarn" ]; then bad "G: the CYCLE warning shows although the global gate is on"; else ok "G: no CYCLE warning while the global gate is on"; fi
cleanup

if [ "$fail" = 0 ]; then echo "r65-diag: all checks passed"; exit 0; fi
echo "r65-diag: FAILED"
exit 1
