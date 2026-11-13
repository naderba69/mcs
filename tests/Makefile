# Smoke test: start the server on a throwaway config and assert the ports
# come up and the web interface answers. Nothing here needs a real card,
# a real peer, or root.
#
#   make -C tests smoke          run the smoke test
#   make -C tests no-profile     assert an empty config is refused, not a segfault
#   make -C tests cache-check-characterization cache-cross-provider cache-cross-gates
#                               run focused cache/provider regressions
#   make -C tests newcamd-ack-race
#                               verify ACK backpressure and the follow-up login
#   make -C tests newcamd-config-reload
#                               verify reload progress during pre-LOGIN receive
BIN     ?= ../make-x64/x64/multics
CFG      = multics-live.cfg
HTTPPORT = 15500
R48_STATS_BIN ?= ../bin/multics-r82a-stats-x64
R48_CWLOG_CHECK ?= 0

.PHONY: pm jm sk cn smoke r48-web no-profile dcwfilter stability ncclient newcamd-ack-race newcamd-config-reload cachecw cwreuse cachepref dstruct cfg19 purgeage agreement agreement-reference entropy timing plaus cyc nt cm pt cg tl ha xs dl ca cu sp ps u ds dt dw xe fl bd pf fs pc rs docrefs cfgcheck prof ce cy au pr soak pq qv rt ecm-radegast-identity cacheex-provider-collision cache-check-characterization cache-cross-provider cache-cross-gates all
include stability.mk

# TASK R48: exercise the real server's LOGIN_ACK socket backpressure path,
# while proving the existing web page and a following login can make progress.
newcamd-ack-race:
	@BIN="$(BIN)" CC="$(CC)" sh ./newcamd-ack-race.sh

# Keep the web-visible reload assertion paired with the production Newcamd
# handshake, rather than treating an isolated socketpair as a lifecycle proof.
newcamd-config-reload:
	@BIN="$(BIN)" CC="$(CC)" sh ./newcamd-config-reload.sh

# soak is deliberately NOT in `all`. It runs for about 20 minutes.
all: smoke r48-web no-profile dcwfilter stability ncclient newcamd-ack-race newcamd-config-reload cachecw cwreuse cachepref dstruct cfg19 purgeage agreement agreement-reference entropy timing plaus cyc nt cm pt cg tl ha xs dl ca cu sp ps u ds dt dw xe fl bd pf fs pc rs docrefs cfgcheck prof ce cy au pr jm pq sk cn vl ri md fk fz at md sc qv cf rt ecm-radegast-identity cacheex-provider-collision cache-check-characterization cache-cross-provider cache-cross-gates

# TASK 4.2. Stats binary, not the stock one: counters must be on.
SOAK_BIN ?= ../bin/multics-r82a-stats-x64
soak: .ncclient.bin .cachepeer.bin
	@chmod +x soak.sh 2>/dev/null || true   # a restored workspace can lose the exec bit
	@test -x $(SOAK_BIN) || { echo "build the stats release first: make -C ../make-x64 release-stats"; exit 1; }
	SOAK_BIN=$(SOAK_BIN) ./soak.sh

# Poll for the HTTP answer instead of sleeping a fixed time. main.c blocks
# startup while /proc/loadavg is high, so under load the server can take well
# over 6 s to answer; a fixed sleep made this target fail intermittently right
# after `make stability` had raised the load.
#
# NOTE: never put a `#` comment inside a recipe here. The backslash continuations
# join the whole recipe into one shell line, so a comment swallows every
# remaining command and variables silently end up empty.
smoke:
	@command -v curl >/dev/null || { echo "curl required"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@rm -f .smoke.log .threads.html .r48.json .r48-cwlog.html .sf.html .sf-strong.html .sf-bad.html; $(BIN) -C $(CFG) > .smoke.log 2>&1 & echo $$! > .pid; \
	code=000; threads_code=000; r48_code=000; cwlog_code=000; sf_code=000; sf_strong_code=000; sf_bad_code=000; tele_ss=000; tele_at=000; tele_ca=000; tele_dg=000; \
	for i in $$(seq 1 25); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/); \
	  [ "$$code" = "200" ] && break; \
	  sleep 1; \
	done; \
	if [ "$$code" = "200" ]; then \
	  threads_code=$$(curl -s -m 5 -u admin:admin -o .threads.html -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/threads); \
	  r48_code=$$(curl -s -m 5 -u admin:admin -o .r48.json -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/json); \
	  sf_code=$$(curl -s -m 5 -u admin:admin -o .sf.html -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/smartfilter); \
	  sf_strong_code=$$(curl -s -m 5 -u admin:admin -o .sf-strong.html -w '%{http_code}' "http://127.0.0.1:$(HTTPPORT)/smartfilter?filter=strongest"); \
	  sf_bad_code=$$(curl -s -m 5 -u admin:admin -o .sf-bad.html -w '%{http_code}' "http://127.0.0.1:$(HTTPPORT)/smartfilter?filter=bad"); \
	  tele_ss=$$(curl -s -m 5 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/server-stats); \
	  tele_at=$$(curl -s -m 5 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/auto-tuning); \
	  tele_ca=$$(curl -s -m 5 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/cache-analytics); \
	  tele_dg=$$(curl -s -m 5 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/diagnostics); \
	  if [ "$(R48_CWLOG_CHECK)" = "1" ]; then cwlog_code=$$(curl -s -m 5 -u admin:admin -o .r48-cwlog.html -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/cwlog); fi; \
	fi; \
	kill $$(cat .pid) 2>/dev/null; rm -f .pid; \
	ok=1; \
	if [ "$$code" = "200" ]; then echo "  [ ok ] HTTP / answered 200"; else echo "  [FAIL] HTTP / answered $$code"; ok=0; fi; \
	if [ "$$threads_code" = "200" ] && grep -q 'THREADID Main =' .threads.html; then echo "  [ ok ] HTTP /threads answered 200 and rendered the thread snapshot"; else echo "  [FAIL] HTTP /threads answered $$threads_code or omitted the snapshot marker"; ok=0; fi; \
	if [ "$$r48_code" = "200" ] && python3 -c 'import json; d=json.load(open(".r48.json")); s=d["r48_source_reputation"]; assert s["scope"] == "source-caid-provider" and isinstance(s["rows"], list)' ; then echo "  [ ok ] HTTP /json exposes the R48 source/provider evidence snapshot"; else echo "  [FAIL] HTTP /json answered $$r48_code or omitted the R48 evidence snapshot"; ok=0; fi; \
	if [ "$$sf_code" = "200" ] && grep -q 'SmartFilter Console' .sf.html && grep -q 'Overview' .sf.html && grep -q 'Recent CW' .sf.html && grep -q 'Peers' .sf.html && grep -q 'Profiles' .sf.html && grep -q 'Logs' .sf.html; then echo "  [ ok ] HTTP /smartfilter renders the v6.0 console (Overview/Recent CW/Peers/Profiles/Logs)"; else echo "  [FAIL] HTTP /smartfilter answered $$sf_code or omitted console sections"; ok=0; fi; \
	if [ "$$sf_strong_code" = "200" ] && [ "$$sf_bad_code" = "200" ]; then echo "  [ ok ] HTTP /smartfilter Strongest/Bad peer filters answered 200"; else echo "  [FAIL] HTTP /smartfilter peer filters answered strongest=$$sf_strong_code bad=$$sf_bad_code"; ok=0; fi; \
	if [ "$$tele_ss" = "200" ] && [ "$$tele_at" = "200" ] && [ "$$tele_ca" = "200" ] && [ "$$tele_dg" = "200" ]; then echo "  [ ok ] HTTP telemetry pages (server-stats/auto-tuning/cache-analytics/diagnostics) answered 200"; else echo "  [FAIL] HTTP telemetry pages answered ss=$$tele_ss at=$$tele_at ca=$$tele_ca dg=$$tele_dg"; ok=0; fi; \
	if [ "$(R48_CWLOG_CHECK)" = "1" ]; then \
	  if [ "$$cwlog_code" = "200" ] && grep -q 'R48 source reputation (reference evidence)' .r48-cwlog.html && grep -q 'proof age / clean samples after proof' .r48-cwlog.html && grep -q 'No reference-confirmed source evidence recorded yet' .r48-cwlog.html; then echo "  [ ok ] HTTP /cwlog renders the R48 evidence panel, D-4 proof timing, and neutral empty state"; else echo "  [FAIL] HTTP /cwlog answered $$cwlog_code or omitted the R48 evidence panel"; ok=0; fi; \
	fi; \
	rm -f .threads.html .r48.json .r48-cwlog.html .sf.html .sf-strong.html .sf-bad.html; \
	if [ "$$ok" = "1" ]; then rm -f .smoke.log; else echo "--- server log (startup only) ---"; cat .smoke.log; exit 1; fi

r48-web:
	@test -x $(R48_STATS_BIN) || { echo "build the stats release first: make -C ../make-x64 release-stats"; exit 1; }
	@$(MAKE) --no-print-directory smoke BIN=$(R48_STATS_BIN) R48_CWLOG_CHECK=1

no-profile:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@printf 'HTTP PORT: 15500\n' > .empty.cfg; \
	$(BIN) -C .empty.cfg >/dev/null 2>&1; rc=$$?; rm -f .empty.cfg; \
	if [ "$$rc" = "1" ]; then echo "  [ ok ] empty config refused with exit 1 (was SIGSEGV/139)"; \
	else echo "  [FAIL] empty config exited $$rc, expected 1"; exit 1; fi

# TASK 3.8b: quoted config values longer than their destination were
# strcpy()'d by parse_quotes without a limit; the server died silently at
# config read. The function-level proof is `make -C ../make-x64 probe-pq`
# and the ASan server run recorded in docs/REPORT-3.8b-ar.md (stack-buffer-
# overflow in parse_quotes, called from read_config:2017). Here the REAL
# plain binary must parse the overlong lines to the end, answer HTTP, stay
# up, and put the TRUNCATED title on the home page (400 chars -> 254).
PQ_HPORT = 15900
PQ_NPORT = 15901
pq:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@rm -f .pq.cfg .pq.log .pq.html; \
	python3 -c "import sys; sys.stdout.write('HTTP PORT: $(PQ_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP TITLE: \"' + 'T'*400 + '\"\nHTTP FILE: \"/x\",\"' + 'U'*600 + '\",\"text/plain\"\nFILE PROVIDERINFO: \"' + 'P'*300 + '\"\n\n[ pqtest ]\nCAID: 1884\nPORT: $(PQ_NPORT)\nUSER: u1 p1\n')" > .pq.cfg; \
	$(BIN) -C .pq.cfg > .pq.log 2>&1 & echo $$! > .pq.pid; \
	code=000; for i in $$(seq 1 25); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(PQ_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; done; \
	kill -0 $$(cat .pq.pid) 2>/dev/null && alive=1 || alive=0; \
	curl -s -m 5 -u admin:admin http://127.0.0.1:$(PQ_HPORT)/ > .pq.html 2>/dev/null; \
	kill $$(cat .pq.pid) 2>/dev/null; rm -f .pq.pid; \
	ok=1; \
	if [ "$$code" = "200" ]; then echo "  [ ok ] HTTP / answered 200 with an overlong quoted config"; else echo "  [FAIL] HTTP / answered $$code"; ok=0; fi; \
	if [ "$$alive" = "1" ]; then echo "  [ ok ] the server survived the overlong quoted config (pre-fix: ASan stack-buffer-overflow at read_config:2017)"; else echo "  [FAIL] the server died parsing the config"; ok=0; fi; \
	if grep -q 'TTTTTTTTTT' .pq.html; then echo "  [ ok ] the truncated title (400 -> 254) is on the home page"; else echo "  [FAIL] the truncated title is not on the home page"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV\|AddressSanitizer" .pq.log; then echo "  [FAIL] the server log has a crash marker"; ok=0; fi; \
	rm -f .pq.cfg .pq.log .pq.html; \
	[ "$$ok" = "1" ]

# TASK 1.10b: proves the real binary parses DCWFILTER REPEAT|CHECKSUM and that
# an unrecognised sub-option is reported by name.
#
# Two environment traps this target exists to work around, both found by hand:
#   - stdout is fully buffered when redirected, and the server never flushes it
#     before it is killed, so `-v` output looks empty. `stdbuf -o0` fixes it.
#   - `timeout` does NOT reliably kill this server: it survives SIGTERM and
#     keeps holding its ports, which makes the next run fail with "bind port
#     failed". Kill by recorded PID and confirm it is gone.
dcwfilter:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@command -v stdbuf >/dev/null || { echo "stdbuf required"; exit 1; }
	@rm -f .filter.log; \
	stdbuf -o0 -e0 $(BIN) -C multics-filter.cfg -v > .filter.log 2>&1 & \
	pid=$$!; \
	sleep 6; \
	kill $$pid 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$pid 2>/dev/null || break; kill -9 $$pid 2>/dev/null; sleep 1; done; \
	ok=1; \
	grep -q "DCWFILTER REPEAT: OFF" .filter.log \
	  && echo "  [ ok ] DCWFILTER REPEAT parsed" \
	  || { echo "  [FAIL] DCWFILTER REPEAT not acknowledged"; ok=0; }; \
	grep -q "DCWFILTER CHECKSUM: OFF" .filter.log \
	  && echo "  [ ok ] DCWFILTER CHECKSUM parsed" \
	  || { echo "  [FAIL] DCWFILTER CHECKSUM not acknowledged"; ok=0; }; \
	grep -q "unknown DCWFILTER option 'BOGUS'" .filter.log \
	  && echo "  [ ok ] unknown sub-option reported by name" \
	  || { echo "  [FAIL] unknown sub-option not reported by name"; ok=0; }; \
	grep -q "SOFT-FAIL-WINDOW: 5000 ms" .filter.log \
	  && echo "  [ ok ] SOFT-FAIL-WINDOW parsed" \
	  || { echo "  [FAIL] SOFT-FAIL-WINDOW not acknowledged"; ok=0; }; \
	grep -q "RETRY-WINDOW: 500 ms" .filter.log \
	  && echo "  [ ok ] RETRY-WINDOW clamped 10 -> 500 ms" \
	  || { echo "  [FAIL] RETRY-WINDOW clamping not applied"; ok=0; }; \
	grep -q "bind port failed" .filter.log \
	  && { echo "  [FAIL] a leftover server is holding the ports"; ok=0; }; \
	rm -f .filter.log; \
	[ $$ok = 1 ]

# Checks that every `file.c:NNN` reference in the documentation still points at a
# real line. Cheap, and it catches the error this project keeps making.
docrefs:
	@sh docrefs.sh

# TASK R40 (D103) -- the shipped profiles file gets a static audit before it is
# booted. prof/qv prove the file STARTS; this proves it MEANS what it says:
# no dead keys, no global-only key inside a profile, and the per-family rules
# from src/casystem.h (09xx half-nulled, iCam strict, fixed keys local).
# Fast and side-effect free, so it runs before the boot targets in `all`.
cfgcheck:
	@python3 cfgcheck.py ../multics-r24-optimized.cfg
	@python3 cfgcheck.py ../multics-r24-expert-reviewed.cfg
	@PYTHONDONTWRITEBYTECODE=1 python3 test_cfgcheck_blind.py

ecm-radegast-identity:
	sh ./ecm-radegast-identity.sh

cacheex-provider-collision:
	@python3 cacheex-provider-collision.py $(BIN)

cache-check-characterization:
	sh ./cache-check-characterization.sh

cache-cross-provider:
	sh ./cache-cross-provider.sh

cache-cross-gates:
	sh ./cache-cross-gates.sh
