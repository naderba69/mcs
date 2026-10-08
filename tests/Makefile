# Smoke test: start the server on a throwaway config and assert the ports
# come up and the web interface answers. Nothing here needs a real card,
# a real peer, or root.
#
#   make -C tests smoke          run the smoke test
#   make -C tests no-profile     assert an empty config is refused, not a segfault
BIN     ?= ../make-x64/x64/multics
CFG      = multics-live.cfg
HTTPPORT = 15500

.PHONY: pm jm sk cn smoke no-profile dcwfilter stability ncclient cachecw cwreuse cachepref dstruct cfg19 purgeage agreement entropy timing plaus cyc nt cm pt cg tl ha xs dl ca cu sp ps u ds dt dw xe fl bd pf fs pc rs docrefs ce cy au pr soak pq oh tl1 preflight all
include stability.mk
# soak is deliberately NOT in `all`. It runs for about 20 minutes.
# Refuse to start a second wing while one is already running. Every target in
# this directory binds fixed ports (cy 16400-16410, ce 15700-15703, oh
# 16996/16997, pq 15900/15901 ...) and writes fixed work files (.cyc.cfg,
# .cyc-srv.log, ...), so two parallel runs do not merely slow each other down:
# they corrupt each other's evidence and print FAILs that do not exist -- the
# exact failure this round paid for twice (evidence-R13/concurrency-collision.log).
# A precise per-target lock is still worth adding on top (M33 in REPORT-R13-ar.md).
all: preflight smoke no-profile dcwfilter stability ncclient cachecw cwreuse cachepref dstruct cfg19 purgeage agreement entropy timing plaus cyc nt cm pt cg tl ha xs dl ca cu sp ps u ds dt dw xe fl bd pf fs pc rs docrefs ce cy au pr jm pq sk cn vl ri oh tl1

preflight:
	@n=$$(ps -eo comm= | grep -c '^multics' || true); \
	if [ "$$n" != "0" ]; then \
	  echo "refusing to start: $$n multics process(es) already running -- another wing is probably active"; \
	  echo "stop it first, or run the single target you want"; \
	  exit 1; \
	fi

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
	@rm -f .smoke.log; $(BIN) -C $(CFG) > .smoke.log 2>&1 & echo $$! > .pid; \
	code=000; \
	for i in $$(seq 1 25); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HTTPPORT)/); \
	  [ "$$code" = "200" ] && break; \
	  sleep 1; \
	done; \
	kill $$(cat .pid) 2>/dev/null; rm -f .pid; \
	if [ "$$code" = "200" ]; then echo "  [ ok ] HTTP / answered 200"; rm -f .smoke.log; else \
	  echo "  [FAIL] HTTP / answered $$code"; echo "--- server log ---"; cat .smoke.log; exit 1; fi

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
