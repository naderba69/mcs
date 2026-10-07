# TASK 1.7 groundwork — runtime stability harness.
#
# WHAT THIS PROVES AND WHAT IT CANNOT.
#
# It proves the real binaries start, bind, answer HTTP, survive a SIGHUP
# hot-reload, and shut down without holding their ports. It therefore proves the
# new .bss tables (cwreuse_tab, trust_tab, purge_tab, rotation_tab) did not break
# startup, and that hot-reload still works with the new globals — neither of
# which any unit test can see, because no unit test links main.c.
#
# It does NOT prove any detector fires. Every protocol that carries a control
# word is either encrypted (CCCam, CS378X, Newcamd/DES) or requires real client
# behaviour (a retry 1-3 s after delivery). There is no unauthenticated path that
# delivers a CW, so this target cannot reach 1.2/1.3/1.5/1.6. The `ncclient`
# target below is what does: it is a real Newcamd client, and it drives the
# server's ECM path end to end.
#
# The environment traps this target works around are the same ones the dcwfilter
# target documents: stdbuf for unbuffered stdout, and kill-by-PID because
# `timeout` does not reliably stop this server.

# Prefixed because `include` shares one namespace with the including Makefile.
# Unprefixed, CFG here silently replaced `CFG = multics-live.cfg` above and
# `smoke` booted with the stability config -- which has no profile, so the
# startup guard refused it and smoke reported a mysterious HTTP 000.
STAB_HPORT = 15700
STAB_NPORT = 15701
STAB_CPORT = 15200
STAB_CFG   = .stability.cfg

.PHONY: stability

# NOTE: no `#` comment may appear inside this recipe. The backslash
# continuations join it into a single shell line, so a comment swallows every
# remaining command -- this broke twice, once making a variable silently empty
# and once producing `/bin/sh: Syntax error: end of file unexpected`.
stability:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@command -v stdbuf >/dev/null || { echo "stdbuf required"; exit 1; }
	@{ printf 'HTTP PORT: $(STAB_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-stability\nTELNET PORT: $(STAB_CPORT)\n\n'; \
	   printf '[ stability ]\nCAID: 1884\nPORT: $(STAB_NPORT)\nUSER: u1 p1\n'; } > $(STAB_CFG); \
	\
	\
	
	\
	run_one() { \
	  bin="$$1"; tag="$$2"; ok=1; \
	  rm -f .stab.$$tag.log; \
	  stdbuf -o0 -e0 "$$bin" -C $(STAB_CFG) -v > .stab.$$tag.log 2>&1 & \
	  pid=$$!; \
	  code=000; \
	  for i in $$(seq 1 25); do \
	    code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(STAB_HPORT)/); \
	    [ "$$code" = "200" ] && break; \
	    sleep 1; \
	  done; \
	  \
	  if kill -0 $$pid 2>/dev/null; then \
	    echo "  [ ok ] $$tag: started and still alive after 6 s"; \
	  else \
	    echo "  [FAIL] $$tag: died during startup"; ok=0; \
	  fi; \
	  \
	  code=$$(curl -s -m 5 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(STAB_HPORT)/ 2>/dev/null); \
	  if [ "$$code" = "200" ]; then \
	    echo "  [ ok ] $$tag: HTTP answered 200"; \
	  else \
	    echo "  [FAIL] $$tag: HTTP answered '$$code'"; ok=0; \
	  fi; \
	  \
	  kill -HUP $$pid 2>/dev/null; sleep 3; \
	  if kill -0 $$pid 2>/dev/null; then \
	    echo "  [ ?? ] $$tag: SURVIVED SIGHUP -- hot-reload now exists, update this test"; \
	  else \
	    echo "  [ ok ] $$tag: SIGHUP terminates it (r82a has no hot-reload handler)"; \
	  fi; \
	  \
	  stdbuf -o0 -e0 "$$bin" -C $(STAB_CFG) -v > .stab.$$tag.log 2>&1 & \
	  pid=$$!; \
	  for i in $$(seq 1 25); do \
	    code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(STAB_HPORT)/); \
	    [ "$$code" = "200" ] && break; \
	    sleep 1; \
	  done; \
	  code=$$(curl -s -m 5 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(STAB_HPORT)/ 2>/dev/null); \
	  if [ "$$code" = "200" ]; then \
	    echo "  [ ok ] $$tag: restarted cleanly and HTTP answered 200 again"; \
	  else \
	    echo "  [FAIL] $$tag: after restart HTTP answered '$$code'"; ok=0; \
	  fi; \
	  \
	  kill $$pid 2>/dev/null; \
	  for i in 1 2 3 4 5; do kill -0 $$pid 2>/dev/null || break; kill -9 $$pid 2>/dev/null; sleep 1; done; \
	  if kill -0 $$pid 2>/dev/null; then \
	    echo "  [FAIL] $$tag: still running after SIGKILL"; ok=0; \
	  else \
	    echo "  [ ok ] $$tag: shut down"; \
	  fi; \
	  \
	  if grep -q "bind port failed" .stab.$$tag.log; then \
	    echo "  [FAIL] $$tag: a leftover server holds the ports"; ok=0; \
	  fi; \
	  if grep -qi "segmentation\|SIGSEGV" .stab.$$tag.log; then \
	    echo "  [FAIL] $$tag: logged a segfault"; ok=0; \
	  fi; \
	  \
	  rm -f .stab.$$tag.log; \
	  if [ $$ok = 1 ]; then return 0; else return 1; fi; \
	}; \
	\
	rc=0; \
	run_one ../make-x64/x64/multics stock || rc=1; \
	sleep 2; \
	run_one $(BIN) configured || rc=1; \
	rm -f $(STAB_CFG); \
	exit $$rc

# A real Newcamd client, built from MultiCS's own des.c / msg-newcamd.c / md5.c
# so the framing and cipher match the server by construction rather than by
# careful copying. send_nonb/recv_nonb/debugf are stubbed locally; linking
# sockets.c and debug.c would drag in the server's globals.
#
# WHY THE "SURVIVED" ASSERTION EXISTS. Wiring the TASK 1.2 retry hook above
# upstream's `if (ecm)` guard in srv-newcamd.c dereferenced `ecm` while it was
# still NULL -- which it always is for a profile's first ECM, because
# search_ecmdata_any() has nothing to find. Every unit test stayed green: none
# of them links main.c or srv-newcamd.c. The server died on the first ECM from a
# correctly authenticated client, which is the single most common thing a real
# deployment does first. This target is the only thing in the suite that runs
# that path, so "the server is still alive afterwards" is asserted explicitly
# rather than left to chance.
#
NCCLIENT   = .ncclient.bin
NC_HPORT   = 15800
NC_NPORT   = 15801
NC_CPORT   = 15300
NC_CFG     = .ncclient.cfg

.PHONY: ncclient

ncclient: $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(NC_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-nctest\nTELNET PORT: $(NC_CPORT)\n\n'; \
	   printf '[ nctest ]\nCAID: 1884\nPORT: $(NC_NPORT)\nUSER: u1 p1\n'; } > $(NC_CFG); \
	rm -f .ncclient.log; \
	stdbuf -o0 -e0 $(BIN) -C $(NC_CFG) -v > .ncclient.log 2>&1 & \
	pid=$$!; \
	 for i in $$(seq 1 25); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(NC_HPORT)/); \
	  [ "$$code" = "200" ] && break; \
	  sleep 1; \
	done; \
	echo "  (http poll: $$code)"; \
	for i in $$(seq 1 15); do \
	  python3 -c "import socket,sys; s=socket.create_connection(('127.0.0.1',$(NC_NPORT)),2); s.close()" 2>/dev/null && break; \
	  sleep 1; \
	done; \
	./$(NCCLIENT) 127.0.0.1 $(NC_NPORT) u1 p1 0102030405060708091011121314 0 > .ncclient.out 2>&1; \
	rc=$$?; \
	if kill -0 $$pid 2>/dev/null; then survived=1; else survived=0; fi; \
	kill $$pid 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$pid 2>/dev/null || break; kill -9 $$pid 2>/dev/null; sleep 1; done; \
	sed 's/^/  /' .ncclient.out; \
	ok=1; \
	grep -q "stage 5: LOGIN_ACK" .ncclient.out || { echo "  [FAIL] the client did not reach LOGIN_ACK"; ok=0; }; \
	grep -q "stage 6: ECM 1/2 sent" .ncclient.out || { echo "  [FAIL] the client did not send the first ECM"; ok=0; }; \
	grep -q "stage 6: ECM 2/2 sent" .ncclient.out || { echo "  [FAIL] the client did not send the second ECM"; ok=0; }; \
	nans=$$(grep -c "decode-failed\|DELIVERED A CONTROL WORD" .ncclient.out); \
	if [ "$$nans" = "2" ]; then \
	  echo "  [ ok ] the server answered both ECMs (decode-failed is correct with no card server)"; \
	else echo "  [FAIL] the server answered $$nans of 2 ECMs"; ok=0; fi; \
	if [ $$survived = 1 ]; then \
	  echo "  [ ok ] the server SURVIVED both ECMs -- regression guard for the NULL ecm deref"; \
	else echo "  [FAIL] the server DIED while processing the ECM"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV" .ncclient.log; then \
	  echo "  [FAIL] the server logged a segfault"; ok=0; \
	fi; \
	grep -q "client 'u1' connected" .ncclient.log || { echo "  [FAIL] the server did not log the client"; ok=0; }; \
	if [ $$rc = 0 ] && [ $$ok = 1 ]; then echo "  [ ok ] a real Newcamd client drove the real server through the ECM path"; fi; \
	rm -f $(NC_CFG) .ncclient.log .ncclient.out; \
	[ $$ok = 1 ] && [ $$rc = 0 ]

$(NCCLIENT): ncclient.c ../src/des.c ../src/msg-newcamd.c ../src/md5.c
	$(CC) -O2 -m64 -std=gnu89 -w -o $@ ncclient.c ../src/des.c ../src/msg-newcamd.c ../src/md5.c
	@chmod +x $@   # a restored workspace can lose the exec bit; the recipe must not depend on it

# TASK 1.2/1.4/1.6 runtime proof -- a real CW round trip.
#
# WHAT THIS ADDS. Until this target existed, no control word had ever been
# delivered by a running server in this project. Every branch of the retry
# classifier, the trust engine and the rotation table is gated on
# `cli->lastecm.status`, which only a real delivery sets -- so 1.2, 1.4 and 1.6
# were unit-tested but never executed. This drives the whole chain:
#
#   client ECM -> server asks its cache peer -> peer pushes a CW -> server
#   delivers it -> client re-asks 2 s later -> RETRY_HARD -> rotation records
#   the source that supplied the key.
#
# The CW arrives through the real `cache_recvmsg()` path, so the origin recorded
# is DCW_SOURCE_CACHE with id PEER_CSP|1 -- which is what makes the GR1
# attribution in the retry line checkable rather than assumed.
#
# TWO TRAPS THIS TARGET EXISTS TO AVOID:
#
#  1. The peer MUST be started before the server. cache_check_peers() pings at
#     ~3 s after startup and then not again for 19 s (the ladder at
#     clustredcache.c:2307-2331 is 9/19/29/59 s). A peer that starts after the
#     HTTP wait misses the first ping and then sits idle for 19 seconds, which
#     looks exactly like "the server never asks". Starting the peer first makes
#     the handshake deterministic.
#  2. The CW must satisfy checksumDCW(), which is a SUM rule and is enabled by
#     default (dcw_filter_checksum=1, config.c:213). A CW that fails it is
#     dropped inside cache_setdcw() with no log line at all. cachepeer.c checks
#     this itself and warns, because a silently dropped CW is
#     indistinguishable from a broken delivery path.
CC_HPORT  = 15900
CC_CACHE  = 15901
CC_PEER   = 15902
CC_NPORT  = 15903
CC_TPORT  = 15910
CC_CFG    = .cachecw.cfg
CC_CW     = 11223366445566FF77889998AABBCC31
CACHEPEER = .cachepeer.bin
NCSERVER  = .ncserver.bin

.PHONY: cachecw

cachecw: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(CC_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-cachecw\nTELNET PORT: $(CC_TPORT)\n'; \
	   printf 'CACHE PORT: $(CC_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(CC_PEER) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n\n'; \
	   printf '[ cachetest ]\nCAID: 1884\nPORT: $(CC_NPORT)\nUSER: u1 p1\n'; } > $(CC_CFG); \
	rm -f .cc-srv.log .cc-peer.log .cc-cli.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(CC_CACHE) $(CC_PEER) $(CC_CW) 1 > .cc-peer.log 2>&1 & \
	peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(CC_CFG) -v > .cc-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CC_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in $$(seq 1 30); do grep -q "advertised card" .cc-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	./$(NCCLIENT) 127.0.0.1 $(CC_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .cc-cli.log 2>&1; \
	if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	ok=1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .cc-cli.log); \
	if [ "$$n" = "2" ]; then echo "  [ ok ] the client RECEIVED A CONTROL WORD, twice"; \
	else echo "  [FAIL] the client received a CW $$n of 2 times"; ok=0; fi; \
	grep -q "dcw=$(CC_CW)" .cc-cli.log \
	  && echo "  [ ok ] and it is exactly the CW the peer pushed" \
	  || { echo "  [FAIL] the delivered CW is not the one the peer pushed"; ok=0; }; \
	grep -q "come Online" .cc-srv.log \
	  && echo "  [ ok ] the server accepted the cache peer" \
	  || { echo "  [FAIL] the server never accepted the cache peer"; ok=0; }; \
	grep -q "cache(GR10): not re-pushing" .cc-srv.log \
	  && echo "  [ ok ] TASK 1.0b: GR10 suppressed the re-push at runtime" \
	  || { echo "  [FAIL] GR10 did not fire"; ok=0; }; \
	grep -q "!~> client 'u1' re-asked" .cc-srv.log \
	  && echo "  [ ok ] TASK 1.2: RETRY_HARD detected at runtime" \
	  || { echo "  [FAIL] the retry was not classified"; ok=0; }; \
	grep -q "after a cw from source 1/65537" .cc-srv.log \
	  && echo "  [ ok ] TASK 1.1: origin is DCW_SOURCE_CACHE / PEER_CSP|1, as GR1 requires" \
	  || { echo "  [FAIL] the retry line did not carry the cache origin"; ok=0; }; \
	grep -q "!!! ROTATION:" .cc-srv.log \
	  && echo "  [ ok ] TASK 1.6: rotation recorded the source to avoid" \
	  || { echo "  [FAIL] rotation did not record anything"; ok=0; }; \
	if [ $$survived = 1 ]; then echo "  [ ok ] the server survived the whole sequence"; \
	else echo "  [FAIL] the server died"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV" .cc-srv.log; then echo "  [FAIL] logged a segfault"; ok=0; fi; \
	rm -f $(CC_CFG) .cc-srv.log .cc-peer.log .cc-cli.log; \
	[ $$ok = 1 ]

$(NCSERVER): ncserver.c ../src/des.c ../src/msg-newcamd.c ../src/md5.c
	$(CC) -O2 -m64 -std=gnu89 -w -o $@ ncserver.c ../src/des.c ../src/msg-newcamd.c ../src/md5.c
	@chmod +x $@

$(CACHEPEER): cachepeer.c
	$(CC) -O2 -m64 -std=gnu89 -Wall -Wextra -o $@ cachepeer.c
	@chmod +x $@   # a restored workspace can lose the exec bit; the recipe must not depend on it

# TASK 1.3 / 1.5 runtime proof -- CW reuse on the CSP cache path, and the purge
# that follows it.
#
# A cache peer pushes the SAME control word for two different services on one
# client connection. That is the definitive proof GR3 allows hard action on: one
# key answering two different ECMs on two different services. The server must
# detect it and mark the poisoned entry.
#
# The second half is the case that matters more. The same peer pushes the same
# key twice for the SAME service -- which is completely normal, because a client
# that fails to decode re-sends the ECM of the crypto period it is still in. If
# that were treated as reuse, every retry would blacklist a working source. GR6
# exists for exactly this, and it is asserted here rather than trusted.
#
# Both ECMs must have different BODIES, not just different headers: ecmd5 and
# the 32-bit hash are computed over ecm[3..] only, so a harness that varies
# ecd.sid alone produces two requests the detector correctly reads as one ECM
# re-sent. That bug was built and found here; ncclient.c now refills the body
# per service.
CWREUSE_CFG = .cachecw.cfg

.PHONY: cwreuse

cwreuse: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(CC_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-cachecw\nTELNET PORT: $(CC_TPORT)\n'; \
	   printf 'CACHE PORT: $(CC_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(CC_PEER) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n\n'; \
	   printf '[ cachetest ]\nCAID: 1884\nPORT: $(CC_NPORT)\nUSER: u1 p1\n'; } > $(CWREUSE_CFG); \
	rc=0; \
	case_one() { \
	  label="$$1"; altsid="$$2"; want="$$3"; \
	  rm -f .cc-srv.log .cc-peer.log .cc-cli.log; \
	  stdbuf -o0 -e0 ./$(CACHEPEER) $(CC_CACHE) $(CC_PEER) $(CC_CW) 2 > .cc-peer.log 2>&1 & \
	  peer=$$!; \
	  sleep 1; \
	  stdbuf -o0 -e0 $(BIN) -C $(CWREUSE_CFG) -v > .cc-srv.log 2>&1 & \
	  srv=$$!; \
	  for i in $$(seq 1 40); do \
	    code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CC_HPORT)/); \
	    [ "$$code" = "200" ] && break; sleep 1; \
	  done; \
	  for i in $$(seq 1 30); do grep -q "advertised card" .cc-peer.log && break; sleep 0.5; done; \
	  sleep 1; \
	  NC_ALT_SID="$$altsid" ./$(NCCLIENT) 127.0.0.1 $(CC_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .cc-cli.log 2>&1; \
	  sleep 1; \
	  if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	  kill $$srv $$peer 2>/dev/null; \
	  for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	  kill -9 $$peer 2>/dev/null; \
	  ncw=$$(grep -c "DELIVERED A CONTROL WORD" .cc-cli.log); \
	  nre=$$(grep -c "CW REUSE PROOF" .cc-srv.log); \
	  npu=$$(grep -c "CACHE PURGE" .cc-srv.log); \
	  if [ "$$ncw" = "2" ]; then echo "  [ ok ] $$label: both ECMs answered with the same cw"; \
	  else echo "  [FAIL] $$label: only $$ncw of 2 ECMs got a cw"; rc=1; fi; \
	  if [ "$$nre" = "$$want" ]; then echo "  [ ok ] $$label: $$nre reuse proof(s), as required"; \
	  else echo "  [FAIL] $$label: $$nre reuse proof(s), expected $$want"; rc=1; fi; \
	  if [ "$$npu" = "$$want" ]; then echo "  [ ok ] $$label: $$npu purge(s), as required"; \
	  else echo "  [FAIL] $$label: $$npu purge(s), expected $$want"; rc=1; fi; \
	  if [ $$survived = 1 ]; then echo "  [ ok ] $$label: server survived"; \
	  else echo "  [FAIL] $$label: server died"; rc=1; fi; \
	  sleep 2; \
	}; \
	case_one "two services, one key" 00C8 1; \
	case_one "GR6: one service, one key" "" 0; \
	rm -f $(CWREUSE_CFG) .cc-srv.log .cc-peer.log .cc-cli.log; \
	exit $$rc

# ---------------------------------------------------------------------------
# TASK 1.7 -- TRUSTED-CACHE-FIRST, proven at runtime.
#
# WHAT IT ASSERTS
#   ON  : a stored cache entry from a peer that has fallen below the trust line
#         is NOT served on a request that has nothing to cross-check it against.
#         The deferral is logged with its evidence, and the ECM is left to the
#         card servers.
#   OFF : the identical run serves that entry, and logs no deferral at all.
#
# Both cases also assert that every client still gets its key. That is the
# property the whole option has to preserve: deferring a stored entry must never
# leave a client with nothing. It holds because the ECM goes on to the card
# servers and, in this topology, to a fresh cache request that the peer answers.
# An operator who turns off both CARD servers and CACHE SENDREQ would remove
# that fallback, which is one more reason the option ships OFF.
#
# The two cases differ in one config line and nothing else, which is the only
# way to show the option caused the outcome rather than the harness happening
# to fail.
#
# WHY THE SETUP LOOKS CONVOLUTED. Three upstream behaviours dictate it, and
# each one was found by instrumenting the cache thread rather than by reading:
#
#   1. A PIPE_CACHE_FIND serve needs a cw_cache_data node without DCW_SENT.
#      Both the FIND and the REQUEST handler set CACHE_FLAG_SENDPIPE on the
#      entry, that flag is never cleared anywhere in the tree, and from then on
#      cache_setdcw() marks DCW_SENT on every CW arriving for the entry. So
#      once this server has asked about a hash, no later push for it can ever be
#      served out of the cache again. The entry has to arrive already populated,
#      which in production happens because another server's client asked first.
#      Hence a peer that pushes an identity it was seeded with (CP_PUSH_HASH).
#
#   2. Re-sending an identical ECM does NOT produce a second FIND -- the ECM
#      object is reused and answered from the ECM table in 0 ms. So one client
#      cannot both populate and then hit the same entry. Hence two profiles,
#      which have independent ECM tables over one shared cache.
#
#   3. Trust is keyed per service (GR4) while cache entries are keyed per hash.
#      To see a deferral the peer must already be distrusted ON THAT SERVICE
#      when the hit happens, so the distrust has to be built through a different
#      ECM identity for the same service. Hence NC_FILL.
#
# The hash to seed is learned from the server's own log in phase A rather than
# hardcoded, so the test cannot rot silently if the ECM body in ncclient.c ever
# changes.
# ---------------------------------------------------------------------------
CP_HPORT  = 15960
CP_CACHE  = 15961
CP_PEER   = 15962
CP_NPORT1 = 15963
CP_NPORT2 = 15964
CP_TPORT  = 15970
CP_CFG    = .cachepref.cfg
CP_CW     = 11223366445566FF77889998AABBCC31

.PHONY: cachepref
cachepref: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@command -v stdbuf >/dev/null || { echo "stdbuf required"; exit 1; }
	@command -v curl >/dev/null || { echo "curl required"; exit 1; }
	@mkcfg() { \
	  { printf 'HTTP PORT: $(CP_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	    printf 'HTTP TITLE: mcs-cachepref\nTELNET PORT: $(CP_TPORT)\n'; \
	    printf 'CACHE PORT: $(CP_CACHE)\n'; \
	    printf 'CACHE PEER: 127.0.0.1:$(CP_PEER) { csp=1 }\n'; \
	    printf 'CACHE FILTER: OFF\nRETRY-WINDOW: 4000\nSOFT-FAIL-WINDOW: 5000\n'; \
	    printf 'TRUSTED-CACHE-FIRST: %s\n\n' "$$1"; \
	    printf '[ prof1 ]\nCAID: 1884\nPORT: $(CP_NPORT1)\nUSER: u1 p1\n\n'; \
	    printf '[ prof2 ]\nCAID: 1884\nPORT: $(CP_NPORT2)\nUSER: u2 p2\n'; } > $(CP_CFG); \
	}; \
	waitup() { \
	  for i in $$(seq 1 40); do \
	    code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CP_HPORT)/); \
	    [ "$$code" = "200" ] && return 0; sleep 1; \
	  done; return 1; \
	}; \
	stopall() { \
	  kill $$srv $$peer 2>/dev/null; \
	  for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	  kill -9 $$peer 2>/dev/null; sleep 2; \
	}; \
	rc=0; seedhash=""; \
	mkcfg OFF; \
	rm -f .cp-srv.log .cp-peer.log .cp-c1.log .cp-c2.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(CP_CACHE) $(CP_PEER) $(CP_CW) 1 > .cp-peer.log 2>&1 & \
	peer=$$!; sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(CP_CFG) -v > .cp-srv.log 2>&1 & \
	srv=$$!; \
	waitup || { echo "  [FAIL] server did not answer HTTP"; exit 1; }; \
	for i in $$(seq 1 30); do grep -q "advertised card" .cp-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECMS=1 NC_FILL=0 ./$(NCCLIENT) 127.0.0.1 $(CP_NPORT1) u1 p1 0102030405060708091011121314 0 1884 0064 > .cp-c1.log 2>&1; \
	stopall; \
	seedhash=$$(grep -o "ecm from client 'u1' ch 1884:000000:0064:[0-9a-f]*" .cp-srv.log | head -1 | sed 's/.*://'); \
	if [ -z "$$seedhash" ]; then echo "  [FAIL] could not learn the ecm hash to seed"; rc=1; \
	else echo "  [ ok ] learned the ecm hash for sid 0064: $$seedhash"; fi; \
	run_case() { \
	  mode="$$1"; wantdefer="$$2"; \
	  mkcfg $$mode; \
	  rm -f .cp-srv.log .cp-peer.log .cp-c1.log .cp-c2.log; \
	  CP_REPUSH_MS=700 CP_PUSH_HASH=$$seedhash CP_PUSH_SID=0064 CP_PUSH_CAID=1884 \
	    stdbuf -o0 -e0 ./$(CACHEPEER) $(CP_CACHE) $(CP_PEER) $(CP_CW) 1 > .cp-peer.log 2>&1 & \
	  peer=$$!; sleep 1; \
	  stdbuf -o0 -e0 $(BIN) -C $(CP_CFG) -v > .cp-srv.log 2>&1 & \
	  srv=$$!; \
	  waitup || { echo "  [FAIL] $$mode: server did not answer HTTP"; rc=1; return; }; \
	  for i in $$(seq 1 30); do grep -q "advertised card" .cp-peer.log && break; sleep 0.5; done; \
	  sleep 2; \
	  NC_ECMS=6 NC_ECM_GAP_MS=1500 NC_FILL=1 ./$(NCCLIENT) 127.0.0.1 $(CP_NPORT1) u1 p1 0102030405060708091011121314 0 1884 0064 > .cp-c1.log 2>&1; \
	  NC_ECMS=3 NC_ECM_GAP_MS=1500 NC_FILL=0 ./$(NCCLIENT) 127.0.0.1 $(CP_NPORT2) u2 p2 0102030405060708091011121314 0 1884 0064 > .cp-c2.log 2>&1; \
	  sleep 1; \
	  if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	  stopall; \
	  ndef=$$(grep -c "CACHE DEFER" .cp-srv.log); \
	  n1=$$(grep -c "DELIVERED A CONTROL WORD" .cp-c1.log); \
	  n2=$$(grep -c "DELIVERED A CONTROL WORD" .cp-c2.log); \
	  nre=$$(grep -c "re-asked" .cp-srv.log); \
	  if [ "$$nre" -ge 2 ]; then echo "  [ ok ] $$mode: $$nre retry events fed the trust engine"; \
	  else echo "  [FAIL] $$mode: only $$nre retry events, the peer was never distrusted"; rc=1; fi; \
	  if [ "$$n1" = "6" ]; then echo "  [ ok ] $$mode: the working client still got all 6 cw(s)"; \
	  else echo "  [FAIL] $$mode: the working client got $$n1 of 6 cw(s)"; rc=1; fi; \
	  if [ "$$wantdefer" = "yes" ]; then \
	    if [ "$$ndef" -ge 1 ]; then echo "  [ ok ] $$mode: $$ndef deferral(s) of the unverified cache entry"; \
	    else echo "  [FAIL] $$mode: no deferral -- the policy did not fire"; rc=1; fi; \
	    if [ "$$n2" -ge 1 ]; then echo "  [ ok ] $$mode: deferring still left the client answered ($$n2 cw(s)) -- no black screen"; \
	    else echo "  [FAIL] $$mode: the second client got nothing at all, the deferral black-screened it"; rc=1; fi; \
	  else \
	    if [ "$$ndef" = "0" ]; then echo "  [ ok ] $$mode: no deferral, stock behaviour kept"; \
	    else echo "  [FAIL] $$mode: $$ndef deferral(s) with the option OFF"; rc=1; fi; \
	    if [ "$$n2" -ge 1 ]; then echo "  [ ok ] $$mode: the same entry WAS served, so ON is what withheld it"; \
	    else echo "  [FAIL] $$mode: the second client got no cw either, so the OFF case proves nothing"; rc=1; fi; \
	  fi; \
	  if [ $$survived = 1 ]; then echo "  [ ok ] $$mode: server survived"; \
	  else echo "  [FAIL] $$mode: server died"; rc=1; fi; \
	}; \
	run_case ON yes; \
	run_case OFF no; \
	rm -f $(CP_CFG) .cp-srv.log .cp-peer.log .cp-c1.log .cp-c2.log; \
	exit $$rc

# ===========================================================================
# TASK 1.8 -- structural pre-filters as soft signals, at runtime.
#
# The unit suite proves what the scan decides. This proves the two things it
# cannot: that the scan is actually reached on the real delivery path, and that
# reaching it changes nothing about what the client gets.
#
# The key the peer pushes is structurally impossible and still perfectly legal:
# its two halves are byte-identical, which is what a forged or corrupted key
# looks like, and both halves satisfy the upstream checksum, so acceptDCW()
# accepts it and the client receives it exactly as it always did. That is the
# whole point of "soft": if the client here got nothing, the task would have
# added a filter, which is the one thing it must not do.
#
# The peer then re-pushes the same key on a timer (CP_REPUSH_MS). The server
# must mention it ONCE and stay quiet afterwards, because the second copy of a
# key is not new evidence -- while the peer's own count shows the pushes really
# happened, so "one line" cannot be explained away as "one push".
# ===========================================================================
DS_HPORT  = 15980
DS_CACHE  = 15981
DS_PEER   = 15982
DS_NPORT  = 15983
DS_TPORT  = 15990
DS_CFG    = .dstruct.cfg
DS_ECM    = 0102030405060708091011121314
# A1B2C316A5A6A7F2 twice: identical halves, checksum-valid.
DS_CW     = A1B2C316A5A6A7F2A1B2C316A5A6A7F2
DS_REPUSH = 1200

.PHONY: dstruct

dstruct: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(DS_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-dstruct\nTELNET PORT: $(DS_TPORT)\n'; \
	   printf 'CACHE PORT: $(DS_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(DS_PEER) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n\n'; \
	   printf '[ structtest ]\nCAID: 1884\nPORT: $(DS_NPORT)\nUSER: u1 p1\n'; } > $(DS_CFG); \
	rm -f .ds-srv.log .ds-peer.log .ds-cli.log; \
	CP_REPUSH_MS=$(DS_REPUSH) CP_REPUSH_SAME=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(DS_CACHE) $(DS_PEER) $(DS_CW) 1 > .ds-peer.log 2>&1 & \
	peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(DS_CFG) -v > .ds-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(DS_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in $$(seq 1 30); do grep -q "advertised card" .ds-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	./$(NCCLIENT) 127.0.0.1 $(DS_NPORT) u1 p1 $(DS_ECM) 0 1884 0064 > .ds-cli.log 2>&1; \
	sleep 2; \
	# The peer re-pushes on a $(DS_REPUSH) ms timer and stops the moment it is \
	# killed below, so the three repeats the later dedupe assertion needs must be \
	# WAITED FOR here, not counted after the fact. 12 s is ten chances. \
	for i in $$(seq 1 12); do \
	  [ $$(grep -c "unsolicited push #" .ds-peer.log) -ge 3 ] && break; \
	  sleep 1; \
	done; \
	if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	ok=1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .ds-cli.log); \
	if [ "$$n" -ge 1 ]; then echo "  [ ok ] the client still received a control word ($$n)"; \
	else echo "[FAIL] no control word reached the client -- the signal became a filter"; ok=0; fi; \
	grep -q "dcw=$(DS_CW)" .ds-cli.log \
	  && echo "  [ ok ] and it is exactly the structurally impossible key, untouched" \
	  || { echo "[FAIL] the delivered key is not the one pushed"; ok=0; }; \
	ns=$$(grep -c "CW STRUCT: HALVES" .ds-srv.log); \
	if [ "$$ns" -ge 1 ]; then echo "  [ ok ] TASK 1.8: the scan ran on the real delivery path ($$ns line(s))"; \
	else echo "[FAIL] the scan never fired: the hook is not on the delivery path"; ok=0; fi; \
	grep -q "CW STRUCT: HALVES from source 1/65537" .ds-srv.log \
	  && echo "  [ ok ] GR1: attributed to DCW_SOURCE_CACHE(1) / PEER_CSP|1, not to a card server" \
	  || { echo "[FAIL] the structural signal was attributed to the wrong source"; ok=0; }; \
	grep -q "trust signal only" .ds-srv.log \
	  && echo "  [ ok ] and the line says so: evidence, not a filter" \
	  || { echo "[FAIL] the log line does not state that delivery is unchanged"; ok=0; }; \
	np=$$(grep -c "unsolicited push #" .ds-peer.log); \
	if [ "$$np" -ge 3 ]; then echo "  [ ok ] the peer re-pushed the SAME key $$np times after the first delivery"; \
	else echo "[FAIL] only $$np repeat push(es): the dedupe below would prove nothing"; ok=0; fi; \
	if [ "$$ns" = "1" ]; then echo "  [ ok ] TASK 1.8: the repeats produced no further line ($$np pushes, 1 report)"; \
	else echo "[FAIL] the same key was reported $$ns times, so the dedupe did not hold"; ok=0; fi; \
	if [ $$survived = 1 ]; then echo "  [ ok ] the server survived the whole sequence"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV" .ds-srv.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	rm -f $(DS_CFG) .ds-srv.log .ds-peer.log .ds-cli.log; \
	[ $$ok = 1 ]

# ===========================================================================
# TASK 1.9 -- the three options, at runtime.
#
# ONE SERVER RUN, ONE CLIENT, TWO PROVEN FACTS.
#
# BAD-CW-LIMIT: the client sends the same ECM four times, two seconds apart, so
# the second, third and fourth arrive inside RETRY-WINDOW and are classified as
# hard retries -- three confirmed bad-CW events from whoever supplied the key.
# With the limit at 3, the first two must be counted and tolerated (one line
# each, at a non-error level) and only the third may record the source for
# avoidance, with exactly one avoidance line. That is the whole point of the
# option: the number of accidents it takes is the operator's, and a default of 1
# is what makes the shipped behaviour the original one.
#
# STATS-WINDOW: the window is set to 2 s against a run that lasts about eight, so
# at least one summary line must appear, and it must name the limit that is in
# force -- a stats line that reported a default while a configured value was
# being used would be worse than no line at all.
#
# The client must still be answered throughout: a tolerance that black-screened
# the channel would be the failure this whole project exists to prevent, so
# "the client got a CW" is asserted, not assumed.
#
# WHAT THIS DOES *NOT* PROVE, so that nobody reads more into a green run than
# there is. The effect BAD-CW-LIMIT has on ROUTING is not observable here.
# rotation_should_avoid_limited() is consulted in exactly one place,
# srvtab_arrange() in loadbalance.c, i.e. when this server is choosing a card
# server; the cache path does not consult rotation at all (grep rotation in
# clustredcache.c returns zero hits), and this config has no card server to
# choose between. So what a green run here proves is the COUNTING and the LOG
# DISCIPLINE; the predicate itself is proven by the unit suite, test_phase1cfg.c.
# Proving the routing effect live would need a mock Newcamd SERVER, which tests/
# does not have. That gap is recorded in STATUS.md rather than papered over --
# and it was found by running a mutation that the harness did NOT catch, which
# is the only reliable way to find out what a test really bites on.
# ===========================================================================
C19_HPORT = 15930
C19_CACHE = 15931
C19_PEER  = 15932
C19_NPORT = 15933
C19_TPORT = 15940
C19_CFG   = .cfg19.cfg
C19_CW    = 11223366445566FF77889998AABBCC31
C19_ECMS  = 4
C19_GAP   = 2000
C19_LIMIT = 3
C19_WIN   = 2000

.PHONY: cfg19

cfg19: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(C19_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-cfg19\nTELNET PORT: $(C19_TPORT)\n'; \
	   printf 'CACHE PORT: $(C19_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(C19_PEER) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'BAD-CW-LIMIT: $(C19_LIMIT)\n'; \
	   printf 'STATS-WINDOW: $(C19_WIN)\n\n'; \
	   printf '[ cfg19 ]\nCAID: 1884\nPORT: $(C19_NPORT)\nUSER: u1 p1\n'; } > $(C19_CFG); \
	rm -f .c19-srv.log .c19-peer.log .c19-cli.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(C19_CACHE) $(C19_PEER) $(C19_CW) 1 > .c19-peer.log 2>&1 & \
	peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(C19_CFG) -v > .c19-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(C19_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in $$(seq 1 30); do grep -q "advertised card" .c19-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECMS=$(C19_ECMS) NC_ECM_GAP_MS=$(C19_GAP) ./$(NCCLIENT) 127.0.0.1 $(C19_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .c19-cli.log 2>&1; \
	sleep 3; \
	if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	ok=1; \
	grep -q "BAD-CW-LIMIT: $(C19_LIMIT)" .c19-srv.log \
	  && echo "  [ ok ] the option was parsed and reported at startup" \
	  || { echo "[FAIL] BAD-CW-LIMIT was not parsed"; ok=0; }; \
	grep -q "STATS-WINDOW: $(C19_WIN)" .c19-srv.log \
	  && echo "  [ ok ] and so was STATS-WINDOW" \
	  || { echo "[FAIL] STATS-WINDOW was not parsed"; ok=0; }; \
	nb=$$(grep -c "BAD-CW LIMIT:.*below the limit of $(C19_LIMIT)" .c19-srv.log); \
	na=$$(grep -c "will avoid source" .c19-srv.log); \
	if [ "$$nb" -ge 1 ]; then echo "  [ ok ] the events below the limit were counted and tolerated ($$nb line(s))"; \
	else echo "[FAIL] no below-limit line: the tolerance did not count anything"; ok=0; fi; \
	if [ "$$na" = "1" ]; then echo "  [ ok ] exactly ONE avoidance line -- one log line per confirmed event, not one per bad cw"; \
	else echo "[FAIL] $$na avoidance line(s), expected exactly 1"; ok=0; fi; \
	if [ "$$nb" = "2" ]; then echo "  [ ok ] and exactly 2 below-limit lines: events 1 and 2 tolerated, event 3 avoided"; \
	else echo "[FAIL] $$nb below-limit line(s), expected 2 for a limit of 3"; ok=0; fi; \
	grep -q "limit reached" .c19-srv.log \
	  && echo "  [ ok ] the avoidance line says the limit was reached, not merely that a retry happened" \
	  || { echo "[FAIL] the avoidance line does not mention the limit"; ok=0; }; \
	ns=$$(grep -c "PHASE1 STATS" .c19-srv.log); \
	if [ "$$ns" -ge 1 ]; then echo "  [ ok ] STATS-WINDOW emitted $$ns summary line(s)"; \
	else echo "[FAIL] STATS-WINDOW produced no line in an eight second run with a two second window"; ok=0; fi; \
	grep -q "PHASE1 STATS.*below limit of $(C19_LIMIT)" .c19-srv.log \
	  && echo "  [ ok ] and the line reports the limit actually in force, not the default" \
	  || { echo "[FAIL] the stats line does not carry the configured limit"; ok=0; }; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .c19-cli.log); \
	if [ "$$n" -ge 1 ]; then echo "  [ ok ] the client was answered throughout ($$n cw(s)) -- the tolerance black-screened nothing"; \
	else echo "[FAIL] the client got nothing: a tolerance must never cost a delivery"; ok=0; fi; \
	if [ $$survived = 1 ]; then echo "  [ ok ] server survived"; \
	else echo "[FAIL] server died"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV" .c19-srv.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	rm -f $(C19_CFG) .c19-srv.log .c19-peer.log .c19-cli.log; \
	[ $$ok = 1 ]

# ---------------------------------------------------------------------------
# TASK 1.9 -- SERVICE-BLACKLIST-TIME, proven at runtime.
#
# WHAT IT ASSERTS
#   ON  (3 s): a definitive reuse proof places a mark on a cache peer, and no key
#              from that peer ever satisfies that channel again -- so the mark is
#              never released by recovery and must be lifted by the clock, once,
#              with the channel and the source named.
#   OFF      : the identical run leaves the same mark standing. This is the half
#              that makes ON meaningful: without it, "an expire line appeared"
#              could be explained by the mark never having been placed at all.
#
# The mark is produced the same way the cwreuse target produces it: one cache
# peer pushing the SAME control word for two different services on one client
# connection, which is the definitive proof GR3 allows hard action on.
#
# The wait is deliberately longer than the window plus one tick of the cache
# thread's three-second wakeup, so a passing run cannot be a race that happened
# to win.
#
# Ports 15950-15953/15959 are this target's; the first cut used 15970/15980,
# which are cachepref's telnet port and dstruct's HTTP port -- a collision that
# would only have shown up when the targets ran in one `make all`, i.e. exactly
# when it is most annoying to debug.
# ---------------------------------------------------------------------------
PA_HPORT = 15950
PA_CACHE = 15951
PA_PEER  = 15952
PA_NPORT = 15953
PA_TPORT = 15959
PA_CFG   = .purgeage.cfg
PA_AGE   = 3000
PA_WAIT  = 7

.PHONY: purgeage

purgeage: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	rc=0; \
	case_one() { \
	  label="$$1"; age="$$2"; want="$$3"; \
	  rm -f .pa-srv.log .pa-peer.log .pa-cli.log; \
	  { printf 'HTTP PORT: $(PA_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	    printf 'HTTP TITLE: mcs-purgeage\nTELNET PORT: $(PA_TPORT)\n'; \
	    printf 'CACHE PORT: $(PA_CACHE)\n'; \
	    printf 'CACHE PEER: 127.0.0.1:$(PA_PEER) { csp=1 }\n'; \
	    printf 'CACHE FILTER: OFF\n'; \
	    [ -n "$$age" ] && printf 'SERVICE-BLACKLIST-TIME: %s\n' "$$age"; \
	    printf '\n[ purgetest ]\nCAID: 1884\nPORT: $(PA_NPORT)\nUSER: u1 p1\n'; } > $(PA_CFG); \
	  stdbuf -o0 -e0 ./$(CACHEPEER) $(PA_CACHE) $(PA_PEER) $(CC_CW) 2 > .pa-peer.log 2>&1 & \
	  peer=$$!; \
	  sleep 1; \
	  stdbuf -o0 -e0 $(BIN) -C $(PA_CFG) -v > .pa-srv.log 2>&1 & \
	  srv=$$!; \
	  for i in $$(seq 1 40); do \
	    code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(PA_HPORT)/); \
	    [ "$$code" = "200" ] && break; sleep 1; \
	  done; \
	  for i in $$(seq 1 30); do grep -q "advertised card" .pa-peer.log && break; sleep 0.5; done; \
	  sleep 1; \
	  NC_ALT_SID=00C8 ./$(NCCLIENT) 127.0.0.1 $(PA_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .pa-cli.log 2>&1; \
	  sleep $(PA_WAIT); \
	  if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	  kill $$srv $$peer 2>/dev/null; \
	  for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	  kill -9 $$peer 2>/dev/null; \
	  nm=$$(grep -c "!!! CACHE PURGE: ch" .pa-srv.log); \
	  ne=$$(grep -c "CACHE PURGE EXPIRE" .pa-srv.log); \
	  if [ "$$nm" = "1" ]; then echo "  [ ok ] $$label: the reuse proof placed exactly 1 mark"; \
	  else echo "[FAIL] $$label: $$nm mark(s), expected 1 -- the scenario did not set up"; rc=1; fi; \
	  if [ "$$ne" = "$$want" ]; then echo "  [ ok ] $$label: $$ne expire line(s), as required"; \
	  else echo "[FAIL] $$label: $$ne expire line(s), expected $$want"; rc=1; fi; \
	  if [ "$$want" = "1" ]; then \
	    chmarked=$$(sed -n 's/.*CACHE PURGE: ch \([0-9a-f:]*\) marked.*/\1/p' .pa-srv.log | head -1); \
	    if [ -n "$$chmarked" ] && grep -q "CACHE PURGE EXPIRE: ch $$chmarked mark on source 1/65537" .pa-srv.log; then \
	      echo "  [ ok ] $$label: the expire line names the SAME channel ($$chmarked) and source the mark line named (GR8)"; \
	    else echo "[FAIL] $$label: the expire line does not name what the mark line named"; rc=1; fi; \
	    grep -q "after $(PA_AGE) ms" .pa-srv.log \
	      && echo "  [ ok ] $$label: and the window it expired against" \
	      || { echo "[FAIL] $$label: the expire line does not state the window"; rc=1; }; \
	  fi; \
	  if [ $$survived = 1 ]; then echo "  [ ok ] $$label: server survived"; \
	  else echo "[FAIL] $$label: server died"; rc=1; fi; \
	  if grep -qi "segmentation\|SIGSEGV" .pa-srv.log; then echo "[FAIL] $$label: logged a segfault"; rc=1; fi; \
	  sleep 2; \
	}; \
	case_one "ON: 3 s window" "$(PA_AGE)" 1; \
	case_one "OFF: the default" "" 0; \
	rm -f $(PA_CFG) .pa-srv.log .pa-peer.log .pa-cli.log; \
	exit $$rc

# ---------------------------------------------------------------------------
# TASK 2.1 -- the agreement ledger, proven at runtime.
#
# WHAT IT ASSERTS, AND WHY THIS IS THE HARDEST ONE TO GET RIGHT
#   TASK 2.1 is the first thing in this project allowed to act on a *single*
#   observation, so the interesting assertions are the ones about what must NOT
#   happen. The target runs two phases against the same ECM:
#
#   PHASE 1 -- two independent cache peers answer with different keys and the
#              client asks ONCE. The disagreement is real and recorded, and
#              nothing may come of it: no accusation, no mark, no score. That is
#              GR3, and it is the half a "positive test" would never catch.
#
#   PHASE 2 -- the same ECM again, now re-asked twice ~2 s apart, which is the
#              only observable failure these protocols have (no negative ACK).
#              The failure lands on the key the client was actually holding, and
#              with the disagreement already on record that is GR3's third
#              definitive proof: agreement mismatch with client failure.
#
#   The accused source is then compared against the source named by the retry
#   line, in the same log. That is the GR1 assertion, and it is written that way
#   because the two peers race: either one may deliver first in a given run, and
#   a test that hardcodes which peer is guilty would pass for the wrong reason.
#
# WHY TWO PEERS AND NOT A MOCK CARD SERVER
#   The cache peers are the harness that exists; a Newcamd *server* mock does
#   not, and this target does not need one, because a cache peer is an
#   independent source exactly as far as this feature is concerned. Both CWs are
#   checksum-valid on purpose: a key that fails checksumDCW() is dropped with no
#   log line, and the run would look like a broken delivery path instead of the
#   feature under test (cachepeer.c warns about this on startup).
#
# The STATS-WINDOW line carries the ledger counters, so phase 1 can be checked
# for the *absence* of a proof before phase 2 has produced one.

AGR_HPORT = 16003
AGR_TPORT = 16010
AGR_CACHE = 16000
AGR_PEERA = 16001
AGR_PEERB = 16002
AGR_NPORT = 16004
AGR_CFG   = .ag.cfg
AGR_WIN   = 2000
AGR_GAP   = 2000
AGR_CWA   = 11223366445566FF77889998AABBCC31
AGR_CWB   = 010203060405060F070809180A0B0C21

agreement: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(AGR_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-agr\nTELNET PORT: $(AGR_TPORT)\n'; \
	   printf 'CACHE PORT: $(AGR_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(AGR_PEERA) { csp=1 }\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(AGR_PEERB) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'STATS-WINDOW: $(AGR_WIN)\n\n'; \
	   printf '[ agr ]\nCAID: 1884\nPORT: $(AGR_NPORT)\nUSER: u1 p1\nUSER: u2 p2\n'; } > $(AGR_CFG); \
	rm -f .ag-srv.log .ag-peera.log .ag-peerb.log .ag-cli1.log .ag-cli2.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(AGR_CACHE) $(AGR_PEERA) $(AGR_CWA) 6 > .ag-peera.log 2>&1 & \
	pa=$$!; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(AGR_CACHE) $(AGR_PEERB) $(AGR_CWB) 6 > .ag-peerb.log 2>&1 & \
	pb=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(AGR_CFG) -v > .ag-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(AGR_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in $$(seq 1 30); do \
	  if grep -q "advertised card" .ag-peera.log && grep -q "advertised card" .ag-peerb.log; then break; fi; \
	  sleep 0.5; \
	done; \
	sleep 1; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(AGR_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .ag-cli1.log 2>&1; \
	for i in $$(seq 1 24); do grep -q "1 dispute, 0 self, 0 proof" .ag-srv.log && break; sleep 0.5; done; \
	cp .ag-srv.log .ag-phase1.log; \
	NC_ECMS=3 NC_ECM_GAP_MS=$(AGR_GAP) ./$(NCCLIENT) 127.0.0.1 $(AGR_NPORT) u2 p2 0102030405060708091011121314 0 1884 0064 > .ag-cli2.log 2>&1; \
	sleep 3; \
	if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	kill $$srv $$pa $$pb 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$pa $$pb 2>/dev/null; \
	ok=1; \
	na1=$$(grep -c "AGREEMENT MISMATCH" .ag-phase1.log); \
	if [ "$$na1" = "0" ]; then echo "  [ ok ] a disagreement alone accused nobody -- GR3, no client failure, no proof"; \
	else echo "[FAIL] $$na1 accusation(s) with no client failure in sight"; ok=0; fi; \
	if grep -q "DELIVERED A CONTROL WORD" .ag-cli1.log; then echo "  [ ok ] phase 1's client was served a key: it is a failure, not a delivery, that accuses"; \
	else echo "[FAIL] phase 1 delivered nothing, so its silence proves nothing"; ok=0; fi; \
	nm1=$$(grep -c "CACHE PURGE: " .ag-phase1.log); \
	if [ "$$nm1" = "0" ]; then echo "  [ ok ] and nothing was marked for it either"; \
	else echo "[FAIL] a mark was placed on a mere disagreement"; ok=0; fi; \
	grep -q "1 dispute, 0 self, 0 proof" .ag-phase1.log \
	  && echo "  [ ok ] the disagreement WAS recorded: 1 dispute, 0 self, 0 proof" \
	  || { echo "[FAIL] the dispute was not recorded, so phase 2 proves nothing"; ok=0; }; \
	nm=$$(grep -c "AGREEMENT MISMATCH" .ag-srv.log); \
	if [ "$$nm" = "1" ]; then echo "  [ ok ] exactly ONE mismatch line for one confirmed event"; \
	else echo "[FAIL] $$nm mismatch line(s), expected exactly 1"; ok=0; fi; \
	acc=$$(grep -o "proof recorded against [0-9]*/[0-9]*" .ag-srv.log | head -1 | sed 's/.*against //'); \
	rt=$$(grep -o "after a cw from source [0-9]*/[0-9]*" .ag-srv.log | head -1 | sed 's/.*source //'); \
	if [ -n "$$acc" ] && [ "$$acc" = "$$rt" ]; then \
	  echo "  [ ok ] GR1: the accused source ($$acc) is the one the client held a key from"; \
	else echo "[FAIL] accused '$$acc' but the retry line names '$$rt'"; ok=0; fi; \
	dis=$$(grep -o "independent source [0-9]*/[0-9]*" .ag-srv.log | head -1 | sed 's/.*source //'); \
	if [ -n "$$dis" ] && [ "$$dis" != "$$acc" ]; then \
	  echo "  [ ok ] the dissenter ($$dis) is named and is NOT the accused"; \
	else echo "[FAIL] the dissenter is missing or is the accused source"; ok=0; fi; \
	if grep -q "CACHE PURGE: .* marked on source $$acc" .ag-srv.log; then \
	  echo "  [ ok ] the cache entry was marked, on the accused source and no other"; \
	else echo "[FAIL] no purge mark naming the accused source"; ok=0; fi; \
	grep -q "1 agreement, 0 dispute" .ag-srv.log && { echo "[FAIL] the stats line disagrees with the mismatch line"; ok=0; } || true; \
	grep -q ", 0 self, 1 proof" .ag-srv.log \
	  && echo "  [ ok ] the ledger counted exactly one proof and no self-disputes" \
	  || { echo "[FAIL] the proof counter is not 1"; ok=0; }; \
	grep -q ", 0 unplaced" .ag-srv.log \
	  && echo "  [ ok ] every failure had an identity to be placed against (0 unplaced)" \
	  || { echo "[FAIL] a failure could not be placed"; ok=0; }; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .ag-cli2.log); \
	if [ "$$n" -ge 1 ]; then echo "  [ ok ] the client kept being served ($$n cw(s)): a proof must not black-screen a channel"; \
	else echo "[FAIL] the client got nothing while the proof was being formed"; ok=0; fi; \
	nt=$$(grep -c "re-asked .*(retry)" .ag-srv.log); \
	if [ "$$nt" -ge 1 ]; then echo "  [ ok ] the re-asks were classified as retries ($$nt), not zaps"; \
	else echo "[FAIL] no retry was classified: the failure signal never fired"; ok=0; fi; \
	if [ $$survived = 1 ]; then echo "  [ ok ] server survived"; else echo "[FAIL] server died"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV" .ag-srv.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	rm -f $(AGR_CFG) .ag-srv.log .ag-phase1.log .ag-peera.log .ag-peerb.log .ag-cli1.log .ag-cli2.log; \
	[ $$ok = 1 ]

# ===========================================================================
# TASK 2.2 -- entropy and collision forensics, live.
#
# WHAT THIS PROVES. One peer, two phases, because the question is what the
# server does with the keys it is handed:
#
#   phase 1  the peer serves the honest key (16 distinct byte values, related to
#            nothing else) for ECM A. The evidence has to be that NOTHING is
#            reported: an anomaly layer that fires on honest traffic is worse
#            than no anomaly layer, and no unit test can show that on a real
#            delivery path.
#
#   phase 2  the same peer serves a key derived from the phase-1 key for ECM B,
#            a different ECM (a second zap: a different SID, hence a different
#            ecm hash, so the cache cannot answer it and the peer must). It is built to be 3 bits away from the first AND
#            still pass checksumDCW(): byte 0 loses one bit, byte 3 is repaired
#            to the new quarter sum. That is not a synthetic nicety, it is the
#            shape of the forgery the public r107 changelog describes (a peer
#            that XORs 0xF0 into a real key's last byte) -- and a key that failed
#            the checksum would be dropped by cache_setdcw() before anything
#            could look at it, so the test would report "no forgery" while
#            proving nothing.
#
# The peer announces the distance it intended ("distance=3 bits from the
# primary") and the server announces the distance it measured ("key 3 bits
# away"); the target asserts BOTH, so the two can disagree out loud instead of
# the test agreeing with itself. The keys named in the line are compared against
# the bytes actually pushed, never against a remembered hex string.
#
# PORTS: 16100 cache, 16101 peer, 16103 http, 16104 newcamd, 16110 telnet.
# Distinct from every other target here (the agreement target owns 16000-16004
# and 16010); the two must never run at the same time.
# ===========================================================================
EN_HPORT = 16103
EN_TPORT = 16110
EN_CACHE = 16100
EN_PEER  = 16101
EN_NPORT = 16104
EN_CFG   = .en.cfg
EN_WIN   = 2000
EN_CW    = 11223366445566FF77889998AABBCC31
EN_CWD   = 10223365445566FF77889998AABBCC31
# The newcamd DES key both clients log in with. It is NOT an ECM: it is the key
# the profile hands out, and a client that logs in with a different one gets a
# "wrong des key" disconnect -- see the note on $5 in ncclient.c.
EN_KEY   = 0102030405060708091011121314
# Two different services, because the ECM hash is what makes them different
# ECMs and the SID is what a real second zap changes.
EN_SID1  = 0064
EN_SID2  = 0065

.PHONY: entropy
entropy: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(EN_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-entropy\nTELNET PORT: $(EN_TPORT)\n'; \
	   printf 'CACHE PORT: $(EN_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(EN_PEER) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'STATS-WINDOW: $(EN_WIN)\n\n'; \
	   printf '[ entropy ]\nCAID: 1884\nPORT: $(EN_NPORT)\nUSER: u1 p1\nUSER: u2 p2\n'; } > $(EN_CFG); \
	rm -f .en-srv.log .en-peer.log .en-cli1.log .en-cli2.log .en-phase1.log; \
	CP_ALT_CW=$(EN_CWD) stdbuf -o0 -e0 ./$(CACHEPEER) $(EN_CACHE) $(EN_PEER) $(EN_CW) 8 > .en-peer.log 2>&1 & \
	peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(EN_CFG) -v > .en-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(EN_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in $$(seq 1 30); do grep -q "advertised card" .en-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	wbefore=$$(grep -c "PHASE1 STATS" .en-srv.log); \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(EN_NPORT) u1 p1 $(EN_KEY) 0 1884 $(EN_SID1) > .en-cli1.log 2>&1; \
	for i in $$(seq 1 24); do \
	  [ "$$(grep -c "PHASE1 STATS" .en-srv.log)" -gt "$$wbefore" ] && break; \
	  sleep 0.5; \
	done; \
	sleep 1; \
	cp .en-srv.log .en-phase1.log; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(EN_NPORT) u2 p2 $(EN_KEY) 0 1884 $(EN_SID2) > .en-cli2.log 2>&1; \
	for i in $$(seq 1 24); do grep -q "CW COLLISION" .en-srv.log && break; sleep 0.5; done; \
	found=0; \
	for i in $$(seq 1 40); do \
	  grep -q "PHASE1 STATS.*1 near (same)" .en-srv.log && { found=1; break; }; \
	  sleep 0.5; \
	done; \
	if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	ok=1; \
	if grep -q "distance=3 bits from the primary" .en-peer.log; then \
	  echo "  [ ok ] the peer measured its own pair of keys: 3 bit flips, checksum still valid"; \
	else echo "[FAIL] the peer's alt key is not the 3-bit edit it was meant to be: $$(grep 'alt cw' .en-peer.log | head -1)"; ok=0; fi; \
	if grep -q "DELIVERED A CONTROL WORD" .en-cli1.log; then \
	  echo "  [ ok ] phase 1's honest key really was delivered (so its silence means something)"; \
	else echo "[FAIL] phase 1 delivered nothing: the negative control proves nothing"; ok=0; fi; \
	ne1=$$(grep -c "CW ENTROPY" .en-phase1.log); \
	nc1=$$(grep -c "CW COLLISION" .en-phase1.log); \
	if [ "$$ne1" = "0" ] && [ "$$nc1" = "0" ]; then \
	  echo "  [ ok ] no entropy and no collision verdict on honest traffic (GR3: it must stay quiet)"; \
	else echo "[FAIL] $$ne1 entropy and $$nc1 collision line(s) on an honest key"; ok=0; fi; \
	grep -q "keys 0 impossible, 0 near (same), 0 near (other)" .en-phase1.log \
	  && echo "  [ ok ] and the stats line counts zero of each, not just the log" \
	  || { echo "[FAIL] the stats line did not show three zeros"; ok=0; }; \
	if grep -q "dcw=$(EN_CWD)" .en-cli2.log; then \
	  echo "  [ ok ] phase 2 delivered the derived key itself, so the verdict is about the key that arrived"; \
	else echo "[FAIL] phase 2 did not deliver the derived key: $$(grep -c 'DELIVERED' .en-cli2.log) delivery(ies)"; ok=0; fi; \
	ncol=$$(grep -c "CW COLLISION" .en-srv.log); \
	if [ "$$ncol" = "1" ]; then echo "  [ ok ] exactly ONE collision line for one derived key"; \
	else echo "[FAIL] $$ncol collision line(s), expected exactly 1"; ok=0; fi; \
	if grep -q "key 3 bits away" .en-srv.log; then \
	  echo "  [ ok ] and the server measured the same 3 bits the peer intended"; \
	else echo "[FAIL] the line does not name the 3-bit distance: $$(grep 'CW COLLISION' .en-srv.log | head -1)"; ok=0; fi; \
	if grep -q "from the key this same source delivered for ecm" .en-srv.log; then \
	  echo "  [ ok ] GR1: both keys came from the same source, so the verdict is attributable"; \
	else echo "[FAIL] the collision was not attributed to this source: $$(grep 'CW COLLISION' .en-srv.log | head -1)"; ok=0; fi; \
	if grep -q "key 10223365" .en-srv.log && grep -q "vs 11223366" .en-srv.log; then \
	  echo "  [ ok ] the line names BOTH keys, byte for byte as the peer pushed them (GR8: exact evidence)"; \
	else echo "[FAIL] the line does not carry both keys"; ok=0; fi; \
	ne2=$$(grep -c "CW ENTROPY" .en-srv.log); \
	if [ "$$ne2" = "0" ]; then echo "  [ ok ] the derived key is not also accused of low entropy (the two halves are independent)"; \
	else echo "[FAIL] $$ne2 entropy line(s) for a key with 16 distinct values"; ok=0; fi; \
	found=0; \
	for i in $$(seq 1 40); do \
	  if grep -q "keys 0 impossible, 1 near (same), 0 near (other)" .en-srv.log; then found=1; break; fi; \
	  sleep 0.5; \
	done; \
	if [ "$$found" = "1" ]; then \
	  echo "  [ ok ] the stats summary counts exactly one same-source near miss (waited for the window, not for a sleep)"; \
	else \
	  echo "[FAIL] the stats summary never counted the collision: $$(grep 'PHASE1 STATS' .en-srv.log | tail -1)"; ok=0; \
	fi; \
	if [ "$$survived" = "1" ]; then echo "  [ ok ] server survived"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	[ "$$ok" = "1" ] || exit 1

# ===========================================================================
# TASK 2.3 -- card-server latency forensics, live.
#
# WHAT THIS PROVES, and why it needs a card server of our own. The verdict is a
# statement about how long a card takes to produce a control word, so the harness
# has to BE the card and decide how long it takes: .ncserver.bin (new) answers the
# newcamd handshake with the server's own crypto and takes a scheduled number of
# milliseconds per reply. MultiCS connects to it as a normal newcamd server, so
# the interval measured is the real one from cli-newcamd.c:298.
#
# THREE PHASES, one server slot, so the window that carries them is one window:
#
#   1. honest jitter (60-66 ms): nothing may fire. This is the negative control,
#      and it comes first: an anomaly layer that reports honest hardware is worse
#      than no layer at all.
#   2. a metronome (exactly 50 ms): FLAT must appear, and -- the part that
#      actually matters -- trust must still be at ZERO entries afterwards,
#      because FLAT is deliberately outside the scoring mask (GR3). That check
#      runs BEFORE phase 3, so a later phase cannot retroactively make it true.
#   3. instant answers (0 ms, below the configured floor): FAST must fire and
#      must score. Trust going 0 -> 8 services across the two phases IS the
#      difference between the two verdicts, and the eight separate services are
#      GR4 shown live: the event lands per (source, CAID, PROVID, SID), never per
#      server.
#
# Every ECM in every phase asks a DIFFERENT service (a different SID, hence a
# different ECM), because a same-service re-ask inside RETRY-WINDOW is a retry:
# the 1.2 classifier would (correctly) re-route away from the card server and
# disconnect the client, and the target would then be measuring TASK 1.2 instead
# of this task. The users rotate over the eight slots the profile defines for the
# same reason one level down -- a second login to a user slot that is still being
# reaped is dropped by the server, which was measured at ~3 ms after LOGIN_ACK.
#
# PORTS: 16200 card server, 16204 newcamd, 16203 http, 16210 telnet.
# Clear of agreement (16000-16010) and entropy (16100-16110).
# ===========================================================================
TN_HPORT = 16203
TN_TPORT = 16210
TN_NPORT = 16204
TN_SPORT = 16200
TN_CFG   = .tn.cfg
TN_WIN   = 2000
TN_KEY   = 0102030405060708091011121314
TN_FLOOR = 42
TN_P1    = 16
TN_P2    = 40
TN_P3    = 8
# The card server's own key, written the way config.c requires it: parse_hex()
# reads a whole run of hex digits, so the 14 bytes must be 14 separate tokens.
TN_SKEY  = 01 02 03 04 05 06 07 08 09 10 11 12 13 14
# 16 jittered over 55-75 ms (20 ms of honest spread), then 40 at exactly 55 ms,
# then instant forever. The three levels come from measuring THIS path, not from
# theory: MultiCS measured a genuinely constant reply as 30-36 ms and a 50 ms one as
# 51-52 ms, so the floor sits at 42 ms -- above the first, below the second -- while
# the honest phase spans 20 ms, far outside the 5 ms flatness band.
TN_DELAYS = 55,70,58,75,60,72,57,74,56,71,59,73,61,68,63,66,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,55,0

.PHONY: timing
timing: $(NCSERVER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(TN_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP TITLE: mcs-timing\n'; \
	   printf 'TELNET PORT: $(TN_TPORT)\n'; \
	   printf 'MIN-CARD-LATENCY: $(TN_FLOOR)\n'; \
	   printf 'STATS-WINDOW: $(TN_WIN)\n'; \
	   printf 'N: 127.0.0.1 $(TN_SPORT) srv1 pass1 $(TN_SKEY)\n\n'; \
	   printf '[ timing ]\nCAID: 1884\nPORT: $(TN_NPORT)\n'; \
	   for u in 1 2 3 4 5 6 7 8; do printf 'USER: u%d p%d\n' $$u $$u; done; } > $(TN_CFG); \
	ok=1; \
	rm -f .tn-srv.log .tn-ns.log .tn-cli.log .tn-p1.log .tn-keys.log; \
	NS_VERBOSE=1 NS_DELAYS=$(TN_DELAYS) stdbuf -o0 -e0 ./$(NCSERVER) $(TN_SPORT) $(TN_KEY) 1884 000000 > .tn-ns.log 2>&1 & \
	ns=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(TN_CFG) -v > .tn-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(TN_HPORT)/ && break; \
	  sleep 1; \
	done; \
	curl -s -m 3 -u admin:admin -o /dev/null 'http://127.0.0.1:$(TN_HPORT)/debug?action=debug&value=SERVER'; \
	for i in $$(seq 1 20); do grep -q "caid 1884 providers 1" .tn-srv.log && break; sleep 0.5; done; \
	grep -q "caid 1884 providers 1" .tn-srv.log \
	  && echo "  [ ok ] the card server connected and advertised CAID 1884" \
	  || { echo "[FAIL] MultiCS never saw a card: the rig proves nothing"; ok=0; }; \
	curl -s -m 3 -u admin:admin -o /dev/null 'http://127.0.0.1:$(TN_HPORT)/debug?action=debug&value=ALL'; \
	{ \
	  for i in $$(seq 1 30); do grep -q "!!! PHASE1 STATS" .tn-srv.log && break; sleep 0.5; done; \
	  grep -q "!!! PHASE1 STATS" .tn-srv.log && echo "  [ ok ] the evidence lines are back: the SERVER filter is a FILTER, not an addition (DBG_ALL=0), and the window is 2 s on a 5 s wakeup" \
	    || { echo "[FAIL] no stats line after ALL: the debug filter is still swallowing DBG_ERROR"; ok=0; }; \
	}; \
	run_ecms() { \
	  n=$$1; base=$$2; tag=$$3; \
	  for k in $$(seq 1 $$n); do \
	    u=$$(( (k - 1) % 8 + 1 )); \
	    sid=$$(printf '%04x' $$(( base + k ))); \
	    NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(TN_NPORT) u$$u p$$u $(TN_KEY) 0 1884 $$sid >> .tn-cli.log 2>&1; \
	    sleep 0.35; \
	  done; \
	  got=$$(grep -c "DELIVERED" .tn-cli.log); \
	  echo "  [ info ] $$tag: $$n ECM(s) on $$n distinct services, $$got control words delivered so far"; \
	}; \
	run_ecms $(TN_P1) 256 phase1-honest; \
	sleep 3; \
	cp .tn-srv.log .tn-p1.log; \
	if [ "$$(grep -c 'CW TIMING' .tn-p1.log)" = "0" ]; then \
	  echo "  [ ok ] phase 1: 16 honest replies (55-75 ms, floor $(TN_FLOOR) ms) produced NO verdict of any kind"; \
	else echo "[FAIL] phase 1: honest jitter produced $$(grep -c 'CW TIMING' .tn-p1.log) verdict line(s)"; ok=0; fi; \
	grep -q "timing 0 fast, 0 flat" .tn-p1.log \
	  && echo "  [ ok ] and the stats line agrees: 0 fast, 0 flat, so the silence is a measurement and not an absence" \
	  || { echo "[FAIL] the stats line does not read 'timing 0 fast, 0 flat': $$(grep 'PHASE1 STATS' .tn-p1.log | tail -1 | sed 's/.*| timing/| timing/')"; ok=0; }; \
	run_ecms $(TN_P2) 512 phase2-metronome; \
	found=0; \
	for i in $$(seq 1 60); do grep -q "!?> CW TIMING" .tn-srv.log && { found=1; break; }; sleep 0.5; done; \
	if [ "$$found" = "1" ]; then echo "  [ ok ] phase 2: 40 replies at exactly 55 ms are reported as a metronome"; \
	else echo "[FAIL] phase 2: no flat verdict after 40 identical 55 ms replies"; ok=0; fi; \
	grep -q "regularity is evidence, not proof" .tn-srv.log \
	  && echo "  [ ok ] and the line says in words that it is not proof, so it cannot be read as an accusation" \
	  || { echo "[FAIL] the flat line does not disclaim itself"; ok=0; }; \
	sleep 3; \
	trust2=$$(grep 'PHASE1 STATS' .tn-srv.log | tail -1 | sed -n 's/.*| trust \([0-9]*\) source.*/\1/p'); \
	if [ "$$trust2" = "0" ]; then \
	  echo "  [ ok ] and it did NOT score: trust is still 0 sources -- FLAT is outside CWT_SCOREMASK (GR3)"; \
	else echo "[FAIL] the flat verdict moved trust to $$trust2 source(s)"; ok=0; fi; \
	grep -q "timing 0 fast, [1-9]" .tn-srv.log \
	  && echo "  [ ok ] a 55 ms reply was never called fast: the floor is a floor and 55 > $(TN_FLOOR)" \
	  || { echo "[FAIL] a 55 ms reply was counted as fast"; ok=0; }; \
	run_ecms $(TN_P3) 768 phase3-instant; \
	for i in $$(seq 1 60); do grep -q "!!! CW TIMING" .tn-srv.log && break; sleep 0.5; done; \
	flines=$$(grep -c "!!! CW TIMING" .tn-srv.log); \
	if [ "$$flines" = "$(TN_P3)" ]; then \
	  echo "  [ ok ] phase 3: instant answers are reported once per service: $$flines lines for $(TN_P3) services"; \
	else echo "[FAIL] $$flines fast line(s) for $(TN_P3) services"; ok=0; fi; \
	grep -q "was not produced by a card" .tn-srv.log \
	  && echo "  [ ok ] and the line states what makes it impossible rather than just scoring it" \
	  || { echo "[FAIL] the fast line does not say why"; ok=0; }; \
	sleep 3; \
	trust3=$$(grep 'PHASE1 STATS' .tn-srv.log | tail -1 | sed -n 's/.*| trust \([0-9]*\) source.*/\1/p'); \
	if [ "$$trust3" = "$(TN_P3)" ]; then \
	  echo "  [ ok ] and THIS one scored: trust went 0 -> $$trust3 sources, one per service -- GR4 granularity, shown live"; \
	else echo "[FAIL] after the fast phase trust is $$trust3 source(s), expected $(TN_P3)"; ok=0; fi; \
	grep -qE "timing $(TN_P3) fast, [1-9][0-9]* flat \([0-9]+ repeat" .tn-srv.log \
	  && echo "  [ ok ] and the stats line counts every reply of both kinds, including the ones the report window suppressed" \
	  || { echo "[FAIL] the timing counters do not add up: $$(grep 'PHASE1 STATS' .tn-srv.log | tail -1 | sed 's/.*| timing/| timing/')"; ok=0; }; \
	grep -q "delayed 0 ms" .tn-ns.log && echo "  [ ok ] the card server confirms it answered instantly" \
	  || { echo "[FAIL] the card server never took a 0 ms turn"; ok=0; }; \
	grep -q "delayed 55 ms" .tn-ns.log && echo "  [ ok ] and that it took its metronome turns" \
	  || { echo "[FAIL] the card server never took a 55 ms turn"; ok=0; }; \
	total=$$(( $(TN_P1) + $(TN_P2) + $(TN_P3) )); \
	d=$$(grep -c "DELIVERED" .tn-cli.log); \
	if [ "$$d" = "$$total" ]; then \
	  echo "  [ ok ] every client was served throughout: $$d of $$total control words delivered, none of this cost a viewer anything"; \
	else echo "[FAIL] only $$d of $$total client requests were answered"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv $$ns 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$ns 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ===========================================================================
# TASK 2.4 -- plausibility scoring, live.
#
# WHAT IS BEING PROVEN. The layer grades a key by how much of its shape survives
# (the four group sums of dcw.c:35) and convicts a source only on a PATTERN: at
# least three keys, in one 60 s window, at 20 % of that source's traffic, all
# failing the SAME group. Three phases, each with its OWN peer on its OWN PORT --
# a cache peer is identified by address, so reusing a port would reuse the source
# and a later phase would pass on the report window instead of on the rate.
#
#   1. honesty control: four good keys, delivered, must produce no line and no
#      counter. An anomaly layer that fires on healthy hardware is worse than no
#      layer.
#   2. the pattern: a peer that answers every ECM with a DIFFERENT forged key
#      (one byte broken, r107's warning shape). Two keys must stay silent; the
#      fourth must produce exactly one line naming the group, the count and the
#      rate, and one soft trust event -- while every key stays counted.
#   3. restraint: one forged key in eleven (9 %, under the 20 % line) must
#      produce no line and no new score, and the ten valid keys must still reach
#      their clients.
#
# WHY A FRESH KEY PER REPLY, and why this target exists at all. The first
# version of it had the peer answer several ECMs with one key, which is a
# CW-reuse proof to TASK 1.3 -- correctly: the cache was purged, the requests
# were re-routed, and the client's next request was never answered. Four replies,
# one delivery. A forger worth defending against edits each key, so the harness
# does too, and the peer prints how far each key is from the last one so the
# target can assert that TASK 2.2's collision detector has nothing to say
# (57-70 bits apart in practice, against a 16-bit threshold).
#
# WRITING THIS TARGET FOUND A REAL BLIND SPOT. `cache_setdcw()` gates with
# `acceptDCW()` BEFORE any evidence hook runs, so a forged key arriving through
# the cache was invisible to every detector -- measured: four forged keys, four
# replies, zero deliveries, a stats line reading `0 full, 0 one-group`. The
# layer was blind, not quiet. The fix offers rejected cache keys to the
# plausibility layer at the gate itself, on the rejecting path only so a key that
# passes is still counted exactly once. That is the path phase 2 of this target
# drives, which is why the target is worth keeping.
#
# The forged keys never reach a client: DCWFILTER CHECKSUM is on by default, so
# they are refused -- and the point of the layer is that the refusal stops being
# anonymous. GR2 is asserted too: no control word means a timeout, so nothing
# here may be recorded as anybody's fault.
#
# PORTS: 16380 http, 16381 cache, 16382/16384/16385 the three peers, 16383
# newcamd, 16390 telnet. Clear of agreement (16000-16010), entropy
# (16100-16110) and timing (16200-16210).
# ===========================================================================
PL_HPORT = 16380
PL_CACHE = 16381
PL_P1    = 16382
PL_P2    = 16384
PL_P3    = 16385
PL_NPORT = 16383
PL_TPORT = 16390
PL_CFG   = .pl.cfg
PL_WIN   = 2000
PL_KEY   = 0102030405060708091011121314
# A good key: all four group sums hold (A1+B2+C3=0x216 -> 16, A5+A6+A7=0x1F2 -> F2).
PL_GOOD  = A1B2C316A5A6A7F2A1B2C316A5A6A7F2
PL_GOOD2 = 10203064112233445566778899AABB45

.PHONY: plaus
plaus: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(PL_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-plaus\nTELNET PORT: $(PL_TPORT)\n'; \
	   printf 'CACHE PORT: $(PL_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(PL_P1) { csp=1 }\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(PL_P2) { csp=1 }\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(PL_P3) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'STATS-WINDOW: $(PL_WIN)\n\n'; \
	   printf '[ plaus ]\nCAID: 1884\nPORT: $(PL_NPORT)\n'; \
	   for u in 1 2 3 4 5 6 7 8 9 10 11 12; do printf 'USER: u%d p%d\n' $$u $$u; done; } > $(PL_CFG); \
	ok=1; \
	rm -f .pl-srv.log .pl-peer.log .pl-cli.log .pl-p1.log; \
	peer=0; \
	start_peer() { \
	  [ "$$peer" != "0" ] && { kill -9 $$peer 2>/dev/null; sleep 0.5; }; \
	  rm -f .pl-peer.log; \
	  env $$3 stdbuf -o0 -e0 ./$(CACHEPEER) $(PL_CACHE) $$1 $$2 64 > .pl-peer.log 2>&1 & \
	  peer=$$!; \
	  for i in $$(seq 1 40); do grep -q "advertised card" .pl-peer.log && break; sleep 0.5; done; \
	  sleep 1; \
	}; \
	ask() { \
	  n=$$1; base=$$2; tag=$$3; \
	  for k in $$(seq 1 $$n); do \
	    u=$$(( (k - 1) % 12 + 1 )); \
	    sid=$$(printf '%04x' $$(( base + k ))); \
	    NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PL_NPORT) u$$u p$$u $(PL_KEY) 0 1884 $$sid >> .pl-cli.log 2>&1; \
	    sleep 0.5; \
	  done; \
	  echo "  [ info ] $$tag: $$n request(s), $$(grep -c 'DELIVERED' .pl-cli.log) control word(s) delivered so far"; \
	}; \
	stats_line() { grep 'PHASE1 STATS' .pl-srv.log | tail -1 | sed 's/.*| shape/| shape/'; }; \
	fresh_stats() { \
	  before=$$(grep -c 'PHASE1 STATS' .pl-srv.log); \
	  for i in $$(seq 1 40); do \
	    [ "$$(grep -c 'PHASE1 STATS' .pl-srv.log)" -gt "$$before" ] && { sleep 0.5; return 0; }; \
	    sleep 0.5; \
	  done; \
	  return 1; \
	}; \
	trust_now() { grep 'PHASE1 STATS' .pl-srv.log | tail -1 | sed -n 's/.*| trust \([0-9]*\) source.*/\1/p'; }; \
	start_peer $(PL_P1) $(PL_GOOD) "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0"; \
	stdbuf -o0 -e0 $(BIN) -C $(PL_CFG) -v > .pl-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(PL_HPORT)/ && break; \
	  sleep 1; \
	done; \
	for i in $$(seq 1 40); do grep -q "advertised card" .pl-peer.log && break; sleep 0.5; done; \
	sleep 2; \
	grep -q "advertised card" .pl-peer.log \
	  && echo "  [ ok ] the first cache peer is online and has advertised its card" \
	  || { echo "[FAIL] no peer online: the rig proves nothing"; ok=0; }; \
	ask 4 256 phase1-honest; \
	fresh_stats; \
	cp .pl-srv.log .pl-p1.log; \
	nd=$$(grep -c "DELIVERED" .pl-cli.log); \
	if [ "$$nd" = "4" ]; then \
	  echo "  [ ok ] phase 1: four honest keys reached four clients (the positive control)"; \
	else echo "[FAIL] phase 1 delivered $$nd of 4"; ok=0; fi; \
	npl=$$(grep -c "CW PLAUSIBILITY" .pl-p1.log); \
	if [ "$$npl" = "0" ]; then \
	  echo "  [ ok ] and produced no plausibility line at all"; \
	else echo "[FAIL] phase 1: $$npl line(s) on healthy keys"; ok=0; fi; \
	grep -q "shape .* full, 0 one-group (0 limited), 0 pattern" .pl-p1.log \
	  && echo "  [ ok ] with the stats line agreeing: every key of full shape, none one-group, no patterns" \
	  || { echo "[FAIL] the stats line does not agree: $$(grep 'PHASE1 STATS' .pl-p1.log | tail -1 | sed 's/.*| shape/| shape/')"; ok=0; }; \
	t0=$$(trust_now); \
	echo "  [ info ] trust before any forgery: $$t0 source(s)"; \
	start_peer $(PL_P2) $(PL_GOOD) "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=1"; \
	ask 2 512 phase2a-two-forged; \
	sleep 4; \
	n2=$$(grep -c "CW PLAUSIBILITY" .pl-srv.log); \
	if [ "$$n2" = "0" ]; then \
	  echo "  [ ok ] phase 2a: two forged keys are counted and NOT convicted -- two is not a pattern"; \
	else echo "[FAIL] phase 2a: $$n2 line(s) after only two keys"; ok=0; fi; \
	ask 2 520 phase2b-four-forged; \
	nd2=$$(grep -c "DELIVERED" .pl-cli.log); \
	for i in $$(seq 1 40); do grep -q "CW PLAUSIBILITY" .pl-srv.log && break; sleep 0.5; done; \
	n2b=$$(grep -c "CW PLAUSIBILITY" .pl-srv.log); \
	if [ "$$n2b" = "1" ]; then \
	  echo "  [ ok ] phase 2b: the third key crossed the line and produced exactly ONE line"; \
	else echo "[FAIL] $$n2b plausibility line(s) after four forged keys, expected exactly 1"; ok=0; fi; \
	grep -q "always group 4" .pl-srv.log \
	  && echo "  [ ok ] and it names WHICH group is broken (4) -- one damaged byte, not four accidents" \
	  || { echo "[FAIL] the line does not name the group: $$(grep 'CW PLAUSIBILITY' .pl-srv.log | head -1 | cut -c1-150)"; ok=0; }; \
	grep -qE "[0-9]+ of its last [0-9]+ keys" .pl-srv.log \
	  && echo "  [ ok ] and carries the count and the rate the verdict was formed on (GR8: the evidence, not just the verdict)" \
	  || { echo "[FAIL] the line has no count/rate evidence"; ok=0; }; \
	grep -q "trust signal only" .pl-srv.log \
	  && echo "  [ ok ] and says in words that delivery was not touched (no silent action)" \
	  || { echo "[FAIL] the line does not disclaim itself"; ok=0; }; \
	fresh_stats; \
	grep -q "4 one-group" .pl-srv.log \
	  && echo "  [ ok ] the stats line counts all four damaged keys, not only the one that was reported" \
	  || { echo "[FAIL] the one-group counter is not 4: $$(stats_line)"; ok=0; }; \
	grep -qE ", 1 pattern" .pl-srv.log \
	  && echo "  [ ok ] and exactly one pattern was announced while every key stayed counted" \
	  || { echo "[FAIL] the pattern counter is not 1: $$(stats_line)"; ok=0; }; \
	t1=$$(trust_now); \
	if [ "$$t1" -gt "$$t0" ]; then \
	  echo "  [ ok ] and it SCORED: trust went $$t0 -> $$t1 entr(ies), for the service that crossed the line (GR4)"; \
	else echo "[FAIL] the pattern did not reach the trust engine: trust stayed at $$t1"; ok=0; fi; \
	nproof=$$(grep 'PHASE1 STATS' .pl-srv.log | tail -1 | sed -n 's/.*, \([0-9]*\) proof,.*/\1/p'); \
	nrot=$$(grep -c "ROTATION" .pl-srv.log); \
	nreuse=$$(grep -c "CW REUSE PROOF" .pl-srv.log); \
	if [ "$$nproof" = "0" ] && [ "$$nrot" = "0" ] && [ "$$nreuse" = "0" ]; then \
	  echo "  [ ok ] GR2/GR3: refused keys produced no proof, no reuse proof, no rotation -- a refused key is a timeout, not an accusation"; \
	else echo "[FAIL] refused keys were treated as somebody's fault: '$$nproof' proof, $$nrot rotation, $$nreuse reuse line(s)"; ok=0; fi; \
	if [ "$$nd2" = "$$nd" ]; then \
	  echo "  [ ok ] and no client received anything from that peer: the checksum filter refused all four forged keys ($$nd delivery(s) before, $$nd2 after)"; \
	else echo "[FAIL] a forged key reached a client: deliveries went $$nd -> $$nd2"; ok=0; fi; \
	ncl=$$(grep -c "CW COLLISION" .pl-srv.log); \
	if [ "$$ncl" = "0" ]; then \
	  echo "  [ ok ] and the collision layer stayed quiet: the forged keys are 57-70 bits apart, far past its 16-bit threshold"; \
	else echo "[FAIL] $$ncl collision line(s): the harness made keys too similar and confounded two layers"; ok=0; fi; \
	start_peer $(PL_P3) $(PL_GOOD) "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=11"; \
	ask 11 768 phase3-one-in-eleven; \
	sleep 5; \
	n3=$$(grep -c "CW PLAUSIBILITY" .pl-srv.log); \
	if [ "$$n3" = "1" ]; then \
	  echo "  [ ok ] phase 3: one forged key in eleven (9 %, under the 20 % line) on a FRESH source added no line"; \
	else echo "[FAIL] phase 3 added $$(( n3 - 1 )) line(s): a single damaged key still convicts"; ok=0; fi; \
	fresh_stats; \
	grep -q "5 one-group" .pl-srv.log \
	  && echo "  [ ok ] it is still counted (five one-group keys in total) and simply never becomes a pattern" \
	  || { echo "[FAIL] the one-group counter is not 5: $$(stats_line)"; ok=0; }; \
	grep -qE ", 1 pattern" .pl-srv.log \
	  && echo "  [ ok ] and the pattern count stayed at 1, so phase 3 changed no score at all" \
	  || { echo "[FAIL] a new pattern appeared in phase 3: $$(stats_line)"; ok=0; }; \
	del=$$(grep -c "DELIVERED" .pl-cli.log); \
	if [ "$$del" = "14" ]; then \
	  echo "  [ ok ] and every valid key in all three phases reached its client (4 + 10): none of this cost a viewer anything"; \
	else echo "[FAIL] $$del deliveries, expected 14 (4 honest + 10 valid in phase 3)"; ok=0; fi; \
	nf=$$(grep -c "group 4 BROKEN" .pl-peer.log); \
	nr=$$(grep -c "group 4 repaired" .pl-peer.log); \
	echo "  [ info ] the third peer reported $$nf broken key(s) and $$nr repaired one(s)"; \
	if [ "$$nf" = "1" ] && [ "$$nr" = "10" ]; then \
	  echo "  [ ok ] and its own instrument confirms the 1-in-11 mix the phase was built from"; \
	else echo "[FAIL] the third peer's key mix is $$nf broken / $$nr repaired, expected 1 / 10"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 2.5 -- cycle/parity visibility on the cache path (observe only).
#
# The rig: one profile whose SID LIST declares cw1cycle=0x81 for every channel,
# so each client ECM (tag 0x80) DECLARES the half it wants: CW0. One CSP peer
# with fwd=1, so its replies may carry the marker byte (buf[29]) that says
# which half the key is.
#
#   phase 1  the peer declares the half it really sent (mark=1 = CW0 = what the
#            ECM asked for): keys are delivered, and the new layer stays silent
#            -- offered 3, marked 3, contra 0, handed 3, 0 stale, no line.
#   phase 2  a fresh peer declares the OPPOSITE half (mark=2 = CW1) on fresh
#            valid keys: the existing Check Cycle gate refuses them, no client
#            sees anything, and the layer reports the contradiction -- exactly
#            ONE line for the source (the second key throttles), naming the
#            halves and saying in words that nothing was done. No trust change:
#            a declared cycle is evidence, not proof (GR3).
#   phase 3  the same peer stops declaring at all (29-byte replies): the gate
#            still refuses the keys (a declaring channel must not take an
#            undeclared key with the filter off), the offered counter grows,
#            the marked counter does not, and STILL no new line: contradiction
#            and silence are different things and the layer must not confuse
#            them.
#
# Everything asserted here is the visibility contract: count, report once, and
# never touch a delivery decision that was not already made.
# ---------------------------------------------------------------------------

CY_HPORT = 16400
CY_CACHE = 16401
CY_P1    = 16402
CY_NPORT = 16403
CY_TPORT = 16410
CY_CFG   = .cyc.cfg
CY_KEY   = 0102030405060708091011121314
CY_GOOD  = A1B2C316A5A6A7F2A1B2C316A5A6A7F2

cyc: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(CY_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-cyc\nTELNET PORT: $(CY_TPORT)\n'; \
	   printf 'CACHE PORT: $(CY_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(CY_P1) { csp=1; fwd=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'STATS-WINDOW: 2000\n\n'; \
	   printf '[ cyc ]\nCAID: 1884\nPORT: $(CY_NPORT)\n'; \
	   printf 'SID LIST: 0300.81 0301.81 0302.81 0303.81 0304.81 0305.81\n'; \
	   for u in 1 2 3 4 5 6 7 8 9 10 11 12; do printf 'USER: u%d p%d\n' $$u $$u; done; } > $(CY_CFG); \
	ok=1; \
	rm -f .cyc-srv.log .cyc-peer.log .cyc-cli.log; \
	peer=0; \
	start_peer() { \
	  [ "$$peer" != "0" ] && { kill -9 $$peer 2>/dev/null; sleep 0.5; }; \
	  rm -f .cyc-peer.log; \
	  env $$3 stdbuf -o0 -e0 ./$(CACHEPEER) $(CY_CACHE) $$1 $$2 64 > .cyc-peer.log 2>&1 & \
	  peer=$$!; \
	  for i in $$(seq 1 40); do grep -q "advertised card" .cyc-peer.log && break; sleep 0.5; done; \
	  sleep 1; \
	}; \
	ask() { \
	  i=0; \
	  for k in $$1; do \
	    i=$$((i+1)); u=$$(( (i + $$2 - 1) % 12 + 1 )); sid=$$k; \
	    NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CY_NPORT) u$$u p$$u $(CY_KEY) 0 1884 $$sid >> .cyc-cli.log 2>&1; \
	    sleep 0.8; \
	  done; \
	}; \
	stats_line() { grep 'PHASE1 STATS' .cyc-srv.log | tail -1 | sed 's/.*| shape/| shape/'; }; \
	cyc_part() { grep 'PHASE1 STATS' .cyc-srv.log | tail -1 | sed -n 's/.*| cycle offered \([0-9]*\) (\([0-9]*\) marked, \([0-9]*\) contra), handed \([0-9]*\) (\([0-9]*\) stale, \([0-9]*\) unverified, \([0-9]*\) line.*/\1 \2 \3 \4 \5 \6 \7/p'; }; \
	fresh_stats() { \
	  before=$$(grep -c 'PHASE1 STATS' .cyc-srv.log); \
	  for i in $$(seq 1 40); do \
	    [ "$$(grep -c 'PHASE1 STATS' .cyc-srv.log)" -gt "$$before" ] && { sleep 0.5; return 0; }; \
	    sleep 0.5; \
	  done; \
	  return 1; \
	}; \
	trust_now() { grep 'PHASE1 STATS' .cyc-srv.log | tail -1 | sed -n 's/.*| trust \([0-9]*\) source.*/\1/p'; }; \
	start_peer $(CY_P1) $(CY_GOOD) "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0 CP_CYCLE_MARK=1"; \
	stdbuf -o0 -e0 $(BIN) -C $(CY_CFG) -v > .cyc-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(CY_HPORT)/ && break; \
	  sleep 1; \
	done; \
	for i in $$(seq 1 40); do grep -q "advertised card" .cyc-peer.log && break; sleep 0.5; done; \
	sleep 2; \
	grep -q "advertised card" .cyc-peer.log \
	  && echo "  [ ok ] the peer is online and has advertised its card" \
	  || { echo "[FAIL] no peer online: the rig proves nothing"; ok=0; }; \
	ask "0300 0301 0302" 0; \
	sleep 3; fresh_stats; \
	cp .cyc-srv.log .cyc-p1.log; \
	nd=$$(grep -c "DELIVERED" .cyc-cli.log); \
	if [ "$$nd" = "3" ]; then \
	  echo "  [ ok ] phase 1: three declared keys, correctly marked, all three delivered"; \
	else echo "[FAIL] phase 1 delivered $$nd of 3"; ok=0; fi; \
	nc1=$$(grep -c "CW CYCLE" .cyc-p1.log); \
	if [ "$$nc1" = "0" ]; then \
	  echo "  [ ok ] and an agreeing source earned no cycle line at all"; \
	else echo "[FAIL] phase 1: $$nc1 cycle line(s) on agreeing keys"; ok=0; fi; \
	c1=$$(cyc_part); \
	if [ "$$c1" = "3 3 0 3 0 0 0" ]; then \
	  echo "  [ ok ] the stats line reads: offered 3 (3 marked, 0 contra), handed 3 (0 stale, 0 unverified, 0 lines)"; \
	else echo "[FAIL] cycle counters are '$$c1', expected '3 3 0 3 0 0 0'"; ok=0; fi; \
	t0=$$(trust_now); \
	start_peer $(CY_P1) $(CY_GOOD) "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0 CP_CYCLE_MARK=2"; \
	ask "0303 0304" 3; \
	sleep 3; \
	nd2=$$(grep -c "DELIVERED" .cyc-cli.log); \
	if [ "$$nd2" = "3" ]; then \
	  echo "  [ ok ] phase 2: both contradictory keys were refused before any client (deliveries stayed 3)"; \
	else echo "[FAIL] a contradictory key reached a client: deliveries 3 -> $$nd2"; ok=0; fi; \
	for i in $$(seq 1 20); do grep -q "CW CYCLE" .cyc-srv.log && break; sleep 0.5; done; \
	sleep 2; \
	nc2=$$(grep -c "CW CYCLE" .cyc-srv.log); \
	if [ "$$nc2" = "1" ]; then \
	  echo "  [ ok ] phase 2: the contradiction produced exactly ONE line -- the second key throttled (one per window)"; \
	else echo "[FAIL] $$nc2 cycle line(s) for two contradictions, expected exactly 1"; ok=0; fi; \
	grep -q "expected CW0, saw CW1" .cyc-srv.log \
	  && echo "  [ ok ] and it names the halves: the ECM declared CW0 and the peer declared CW1" \
	  || { echo "[FAIL] the line does not name the halves: $$(grep 'CW CYCLE' .cyc-srv.log | head -1 | cut -c1-160)"; ok=0; }; \
	grep -q "offered by the peer" .cyc-srv.log \
	  && echo "  [ ok ] and says WHERE it saw it (offered by the peer, not handed to a client)" \
	  || { echo "[FAIL] the line does not name the observation point"; ok=0; }; \
	grep -q "Nothing was rejected, delayed or scored" .cyc-srv.log \
	  && echo "  [ ok ] and says in words that nothing was done -- a measurement, not an action" \
	  || { echo "[FAIL] the line does not disclaim itself"; ok=0; }; \
	t1=$$(trust_now); \
	if [ "$$t1" = "$$t0" ]; then \
	  echo "  [ ok ] GR3: a declared cycle is evidence, not proof -- trust stayed at $$t1 source(s)"; \
	else echo "[FAIL] the contradiction changed trust: $$t0 -> $$t1"; ok=0; fi; \
	fresh_stats; \
	c2=$$(cyc_part); \
	if [ "$$c2" = "5 5 2 3 0 0 1" ]; then \
	  echo "  [ ok ] the stats line reads: offered 5 (5 marked, 2 contra), handed 3 (0 stale, 0 unverified, 1 line)"; \
	else echo "[FAIL] cycle counters are '$$c2', expected '5 5 2 3 0 0 1'"; ok=0; fi; \
	start_peer $(CY_P1) $(CY_GOOD) "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0 CP_CYCLE_MARK=0"; \
	ask "0305" 5; \
	sleep 3; fresh_stats; \
	nd3=$$(grep -c "DELIVERED" .cyc-cli.log); \
	nc3=$$(grep -c "CW CYCLE" .cyc-srv.log); \
	if [ "$$nd3" = "3" ] && [ "$$nc3" = "1" ]; then \
	  echo "  [ ok ] phase 3: the undeclared key was refused by the gate (deliveries 3) and earned NO new line"; \
	else echo "[FAIL] phase 3: deliveries $$nd3 (want 3), cycle lines $$nc3 (want 1)"; ok=0; fi; \
	c3=$$(cyc_part); \
	if [ "$$c3" = "6 5 2 3 0 0 1" ]; then \
	  echo "  [ ok ] and the layer kept the two apart: offered 6 but only 5 marked -- silence counted, never accused"; \
	else echo "[FAIL] cycle counters are '$$c3', expected '6 5 2 3 0 0 1'"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 2.6 -- negative memory ("never twice"), live.
#
# THE SCENARIO. One CSP peer, one key (NT_CW), one connection asking THREE
# services in sequence (sids 0064 / 00C8 / 012C -- ncclient's NC_ALT_SID and
# the new NC_ALT_SID2):
#
#   1. service 1 asks, the peer answers with the key: delivered (nobody has
#      proven anything yet).
#   2. service 2 asks, the peer answers with the SAME key for a different ECM
#      on a different service: the TASK 1.3 reuse proof fires, and with it the
#      key itself is filed into negative memory ("CW NEGATIVE MARK").
#   3. service 3 asks, the peer answers with the SAME key a third time: the
#      gate refuses it before storage, before peer agreement, before any
#      client. Deliveries stay at 2, one refusal line is written, and the
#      stats line shows the memory: 1 live, 1 proven, 1 refused.
#
# What this proves is the property the task is named for: a key that a
# definitive proof convicted cannot be delivered a second time -- the third
# reply is byte-identical to the first two, and this time nothing reaches a
# client. Also pinned: a re-proof of the same key does not write a second
# MARK line (the reuse detector proves it again on service 3 -- keyed per ECM
# identity -- and the negative table answers that quietly), and no score, no
# purge count and no source state changes at the refusal itself.
# ---------------------------------------------------------------------------

NT_HPORT = 16500
NT_CACHE = 16501
NT_PEER  = 16502
NT_NPORT = 16503
NT_TPORT = 16510
NT_CFG   = .nt.cfg
NT_CW    = 11223366445566FF77889998AABBCC31
NT_KEY   = 0102030405060708091011121314

.PHONY: nt
nt: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(NT_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-nt\nTELNET PORT: $(NT_TPORT)\n'; \
	   printf 'CACHE PORT: $(NT_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(NT_PEER) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'STATS-WINDOW: 2000\n\n'; \
	   printf '[ never-twice ]\nCAID: 1884\nPORT: $(NT_NPORT)\nUSER: u1 p1\n'; } > $(NT_CFG); \
	ok=1; \
	rm -f .nt-srv.log .nt-peer.log .nt-cli.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(NT_CACHE) $(NT_PEER) $(NT_CW) 4 > .nt-peer.log 2>&1 & \
	peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(NT_CFG) -v > .nt-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(NT_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in $$(seq 1 30); do grep -q "advertised card" .nt-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	grep -q "advertised card" .nt-peer.log \
	  && echo "  [ ok ] the peer is online and has advertised its card" \
	  || { echo "[FAIL] no peer online: the rig proves nothing"; ok=0; }; \
	NC_ECMS=3 NC_ALT_SID=00C8 NC_ALT_SID2=012C NC_ECM_GAP_MS=2500 \
	  ./$(NCCLIENT) 127.0.0.1 $(NT_NPORT) u1 p1 $(NT_KEY) 0 1884 0064 > .nt-cli.log 2>&1; \
	sleep 3; \
	nmark=$$(grep -c "CW NEGATIVE MARK" .nt-srv.log); \
	if [ "$$nmark" = "1" ]; then \
	  echo "  [ ok ] phase 2: the reuse proof filed the key itself, exactly once (CW NEGATIVE MARK)"; \
	else echo "[FAIL] $$nmark negative mark line(s), expected exactly 1"; ok=0; fi; \
	grep -q "it will not be delivered again, to any client, from any source" .nt-srv.log \
	  && echo "  [ ok ] and the mark says in words what it now guarantees" \
	  || { echo "[FAIL] the mark line does not state its guarantee"; ok=0; }; \
	nref=$$(grep -c "CW NEGATIVE: " .nt-srv.log); \
	if [ "$$nref" = "1" ]; then \
	  echo "  [ ok ] phase 3: the third offer of the same poison was refused, one line"; \
	else echo "[FAIL] $$nref refusal line(s), expected exactly 1"; ok=0; fi; \
	grep -q "refused before any client" .nt-srv.log \
	  && echo "  [ ok ] and the refusal names the moment it acted: before any client" \
	  || { echo "[FAIL] the refusal line does not name its place in the pipeline"; ok=0; }; \
	grep -q "not a score: nothing was scored and no source was disabled" .nt-srv.log \
	  && echo "  [ ok ] and says in words that the refusal moved no score and disabled nothing" \
	  || { echo "[FAIL] the refusal line does not disclaim itself"; ok=0; }; \
	nre=$$(grep -c "CW REUSE PROOF" .nt-srv.log); \
	[ "$$nre" -ge 1 ] && echo "  [ ok ] the reuse proof(s) behind the mark: $$nre" \
	  || { echo "[FAIL] no reuse proof: the mark was filed without its evidence"; ok=0; }; \
	ncw=$$(grep -c "DELIVERED A CONTROL WORD" .nt-cli.log); \
	if [ "$$ncw" = "2" ]; then \
	  echo "  [ ok ] GR3 in the positive direction: exactly the two pre-proof deliveries happened, the third reply reached nobody"; \
	else echo "[FAIL] $$ncw delivery(ies), expected 2 (services 1 and 2, before the proof)"; ok=0; fi; \
	fresh_stats() { \
	  before=$$(grep -c 'PHASE1 STATS' .nt-srv.log); \
	  for i in $$(seq 1 40); do \
	    [ "$$(grep -c 'PHASE1 STATS' .nt-srv.log)" -gt "$$before" ] && { sleep 0.5; return 0; }; \
	    sleep 0.5; \
	  done; \
	  return 1; \
	}; \
	fresh_stats; \
	neg=$$(grep 'PHASE1 STATS' .nt-srv.log | tail -1 | sed -n 's/.*| negative \([0-9]*\) live (\([0-9]*\) proven, \([0-9]*\) refused, \([0-9]*\) line.*/\1 \2 \3 \4/p'); \
	if [ "$$neg" = "1 1 1 1" ]; then \
	  echo "  [ ok ] the stats line remembers: 1 live, 1 proven, 1 refused, 1 line"; \
	else echo "[FAIL] negative counters are '$$neg', expected '1 1 1 1'"; ok=0; fi; \
	grep -q "never delivered again" .nt-srv.log && true; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 2.7 -- complement-mirror detection, live.
#
# THE SCENARIO. One CSP peer. In phase 1 it answers with fresh VALID keys.
# In phases 2-3 it answers with fresh MIRROR-BUILT keys (CP_MIRROR=1): half 2
# is the bitwise complement of half 1, and the two group sums the complement
# breaks are repaired -- so the key passes checksumDCW with 4 of 4 groups and
# is DELIVERED. That is the point: phase 2 pins that the mirror keys reach
# clients AND that the plausibility layer (2.4) reads them as full shape --
# nothing in the log explains the black screen they cause. The mirror layer
# counts them silently (two keys are not a pattern), and the third one
# crosses it: exactly ONE line naming the construction, and a soft trust
# event (GR3: edit evidence, not proof).
# ---------------------------------------------------------------------------

CM_HPORT = 16600
CM_CACHE = 16601
CM_P1    = 16602
CM_NPORT = 16603
CM_TPORT = 16610
CM_CFG   = .cm.cfg
CM_KEY   = 0102030405060708091011121314
CM_GOOD  = A1B2C316A5A6A7F2A1B2C316A5A6A7F2

.PHONY: cm
cm: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(CM_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-cm\nTELNET PORT: $(CM_TPORT)\n'; \
	   printf 'CACHE PORT: $(CM_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(CM_P1) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'STATS-WINDOW: 2000\n\n'; \
	   printf '[ mirror ]\nCAID: 1884\nPORT: $(CM_NPORT)\n'; \
	   for u in 1 2 3 4 5 6 7 8 9 10 11 12; do printf 'USER: u%d p%d\n' $$u $$u; done; } > $(CM_CFG); \
	ok=1; \
	rm -f .cm-srv.log .cm-peer.log .cm-cli.log; \
	peer=0; \
	start_peer() { \
	  [ "$$peer" != "0" ] && { kill -9 $$peer 2>/dev/null; sleep 0.5; }; \
	  rm -f .cm-peer.log; \
	  env $$3 stdbuf -o0 -e0 ./$(CACHEPEER) $(CM_CACHE) $$1 $$2 64 > .cm-peer.log 2>&1 & \
	  peer=$$!; \
	  for i in $$(seq 1 40); do grep -q "advertised card" .cm-peer.log && break; sleep 0.5; done; \
	  sleep 1; \
	}; \
	ask() { \
	  i=0; \
	  for k in $$1; do \
	    i=$$((i+1)); u=$$(( (i + $$2 - 1) % 12 + 1 )); sid=$$k; \
	    NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CM_NPORT) u$$u p$$u $(CM_KEY) 0 1884 $$sid >> .cm-cli.log 2>&1; \
	    sleep 0.8; \
	  done; \
	}; \
	mirror_part() { grep 'PHASE1 STATS' .cm-srv.log | tail -1 | sed -n 's/.*| mirror \([0-9]*\) keys, \([0-9]*\) complement-built (\([0-9]*\) pattern.*/\1 \2 \3/p'; }; \
	fresh_stats() { \
	  before=$$(grep -c 'PHASE1 STATS' .cm-srv.log); \
	  for i in $$(seq 1 40); do \
	    [ "$$(grep -c 'PHASE1 STATS' .cm-srv.log)" -gt "$$before" ] && { sleep 0.5; return 0; }; \
	    sleep 0.5; \
	  done; \
	  return 1; \
	}; \
	trust_now() { grep 'PHASE1 STATS' .cm-srv.log | tail -1 | sed -n 's/.*| trust \([0-9]*\) source.*/\1/p'; }; \
	start_peer $(CM_P1) $(CM_GOOD) "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0 CP_MIRROR=0"; \
	stdbuf -o0 -e0 $(BIN) -C $(CM_CFG) -v > .cm-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(CM_HPORT)/ && break; \
	  sleep 1; \
	done; \
	for i in $$(seq 1 40); do grep -q "advertised card" .cm-peer.log && break; sleep 0.5; done; \
	sleep 2; \
	grep -q "advertised card" .cm-peer.log \
	  && echo "  [ ok ] the peer is online and has advertised its card" \
	  || { echo "[FAIL] no peer online: the rig proves nothing"; ok=0; }; \
	ask "0400 0401 0402" 0; \
	sleep 3; fresh_stats; \
	nd=$$(grep -c "DELIVERED" .cm-cli.log); \
	if [ "$$nd" = "3" ]; then \
	  echo "  [ ok ] phase 1: three honest keys, three deliveries"; \
	else echo "[FAIL] phase 1 delivered $$nd of 3"; ok=0; fi; \
	nc1=$$(grep -c "CW MIRROR" .cm-srv.log); \
	c1=$$(mirror_part); \
	if [ "$$nc1" = "0" ] && [ "$$c1" = "3 0 0" ]; then \
	  echo "  [ ok ] the mirror layer saw all three and said nothing: mirror 3 keys, 0 complement-built"; \
	else echo "[FAIL] phase 1: $$nc1 line(s), counters '$$c1', expected none and '3 0 0'"; ok=0; fi; \
	t0=$$(trust_now); \
	start_peer $(CM_P1) $(CM_GOOD) "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0 CP_MIRROR=1"; \
	ask "0403 0404" 3; \
	sleep 3; fresh_stats; \
	nd2=$$(grep -c "DELIVERED" .cm-cli.log); \
	if [ "$$nd2" = "5" ]; then \
	  echo "  [ ok ] phase 2: both mirror keys PASSED the checksum layer and were DELIVERED (deliveries 3 -> 5)"; \
	else echo "[FAIL] mirror deliveries went 3 -> $$nd2, expected 5 (the construction is deliverable by design)"; ok=0; fi; \
	npl=$$(grep -c "CW PLAUSIBILITY" .cm-srv.log); \
	if [ "$$npl" = "0" ]; then \
	  echo "  [ ok ] and 2.4 read them as full shape: zero plausibility lines -- the construction is invisible to every structural layer"; \
	else echo "[FAIL] $$npl plausibility line(s): the mirror should read 4/4 to 2.4"; ok=0; fi; \
	nm1=$$(grep -c "CW MIRROR" .cm-srv.log); \
	c2=$$(mirror_part); \
	if [ "$$nm1" = "0" ] && [ "$$c2" = "5 2 0" ]; then \
	  echo "  [ ok ] the mirror layer counted both silently: mirror 5 keys, 2 complement-built, still no line (two keys are not a pattern)"; \
	else echo "[FAIL] phase 2: $$nm1 line(s), counters '$$c2', expected '5 2 0'"; ok=0; fi; \
	t1=$$(trust_now); \
	if [ "$$t1" = "$$t0" ]; then \
	  echo "  [ ok ] and no score moved on two keys (GR3: one key never convicts)"; \
	else echo "[FAIL] trust moved on two keys: $$t0 -> $$t1"; ok=0; fi; \
	ask "0405" 6; \
	sleep 3; \
	for i in $$(seq 1 20); do grep -q "CW MIRROR" .cm-srv.log && break; sleep 0.5; done; \
	sleep 2; \
	nm2=$$(grep -c "CW MIRROR" .cm-srv.log); \
	if [ "$$nm2" = "1" ]; then \
	  echo "  [ ok ] phase 3: the third mirror key crossed the pattern -- exactly ONE line"; \
	else echo "[FAIL] $$nm2 mirror line(s), expected exactly 1"; ok=0; fi; \
	grep -q "bitwise complement" .cm-srv.log \
	  && echo "  [ ok ] and it names the construction: the second half is the complement of the first" \
	  || { echo "[FAIL] the line does not name the construction"; ok=0; }; \
	grep -q "6 of 6 byte pairs" .cm-srv.log \
	  && echo "  [ ok ] and carries the pair count the verdict was formed on (GR8)" \
	  || { echo "[FAIL] the line has no pair count"; ok=0; }; \
	grep -q "Delivery was not touched by this layer" .cm-srv.log \
	  && echo "  [ ok ] and says in words that delivery was not touched" \
	  || { echo "[FAIL] the line does not disclaim itself"; ok=0; }; \
	nd3=$$(grep -c "DELIVERED" .cm-cli.log); \
	if [ "$$nd3" = "6" ]; then \
	  echo "  [ ok ] the mirror key was delivered anyway -- the layer observes, it does not gate (the gate belongs to a later decision)"; \
	else echo "[FAIL] deliveries $$nd3, expected 6"; ok=0; fi; \
	t2=$$(trust_now); \
	if [ "$$t2" -gt "$$t1" ]; then \
	  echo "  [ ok ] and it SCORED: trust went $$t1 -> $$t2 entr(ies) -- edit evidence, softly (GR3/GR4)"; \
	else echo "[FAIL] the pattern did not reach the trust engine: trust stayed $$t2"; ok=0; fi; \
	nre=$$(grep -c "CW REUSE PROOF" .cm-srv.log); \
	nneg=$$(grep -c "CW NEGATIVE" .cm-srv.log); \
	if [ "$$nre" = "0" ] && [ "$$nneg" = "0" ]; then \
	  echo "  [ ok ] no cross-fire: fresh keys triggered no reuse proof and no negative memory"; \
	else echo "[FAIL] cross-fire: $$nre reuse proof(s), $$nneg negative line(s)"; ok=0; fi; \
	fresh_stats; \
	c3=$$(mirror_part); \
	if [ "$$c3" = "6 3 1" ]; then \
	  echo "  [ ok ] the stats line remembers: mirror 6 keys, 3 complement-built, 1 pattern"; \
	else echo "[FAIL] mirror counters are '$$c3', expected '6 3 1'"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1


# ---------------------------------------------------------------------------
# TASK 2.8 -- the cache-peer trust engine (coarse tier), live.
#
# THE SCENARIO. One CSP peer, B. Phase 1: B answers three DIFFERENT services
# with fresh VALID keys -- three deliveries, nobody below the line, no
# crossing. Phase 2: B turns reuse-poisoner (the same fixed key for every
# service). Three fresh services: the first fixes the key's identity, the
# second offer is one CW-reuse proof, the third offer is the second proof --
# and the COARSE tier sums both at (peer B, caid 1884): one proof above the
# line, two below it, so the crossing fires exactly once and the sentence
# names the counts. Phase 3: peer A (honest) joins and a seventh service is
# asked. B's share of the request fan-out is skipped -- its request count
# freezes at three while A's grows -- and the client still gets its key from
# A. That last part is the point of the whole engine: skipping a poisoner
# must never cost a client anything.
# ---------------------------------------------------------------------------

PT_HPORT = 16620
PT_CACHE = 16621
PT_A     = 16622
PT_B     = 16623
PT_NPORT = 16624
PT_TPORT = 16630
PT_CFG   = .pt.cfg
PT_KEY   = 0102030405060708091011121314
# Sixteen DISTINCT bytes, all four group sums valid, zero complement pairs --
# a key that no structural detector (2.2 low-diversity, 2.7 mirror) may say
# anything about. The only signal it may ever produce is the reuse proof.
PT_GOOD  = A54DCABC2530BB106D132CACD6237B74

.PHONY: pt
pt: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(PT_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-pt\nTELNET PORT: $(PT_TPORT)\n'; \
	   printf 'CACHE PORT: $(PT_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(PT_B) { csp=1 }\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(PT_A) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'PEER-TRUST-REQUESTS: ON\n'; \
	   printf 'STATS-WINDOW: 2000\n\n'; \
	   printf '[ pt ]\nCAID: 1884\nPORT: $(PT_NPORT)\n'; \
	   for u in 1 2 3 4 5 6 7 8 9 10 11 12; do printf 'USER: u%d p%d\n' $$u $$u; done; } > $(PT_CFG); \
	ok=1; \
	rm -f .pt-srv.log .pt-b.log .pt-a.log .pt-cli.log; \
	peerb=0; peera=0; \
	start_b() { \
	  [ "$$peerb" != "0" ] && { kill -9 $$peerb 2>/dev/null; sleep 0.5; }; \
	  rm -f .pt-b.log; \
	  env $$1 stdbuf -o0 -e0 ./$(CACHEPEER) $(PT_CACHE) $(PT_B) $(PT_GOOD) 32 > .pt-b.log 2>&1 & \
	  peerb=$$!; \
	  sleep 1.5; \
	}; \
	start_a() { \
	  rm -f .pt-a.log; \
	  env $$1 stdbuf -o0 -e0 ./$(CACHEPEER) $(PT_CACHE) $(PT_A) $(PT_GOOD) 32 > .pt-a.log 2>&1 & \
	  peera=$$!; \
	  for i in $$(seq 1 40); do grep -q "advertised card" .pt-a.log && break; sleep 0.5; done; \
	  for i in $$(seq 1 40); do grep -q "Online.*$(PT_A)" .pt-srv.log && break; sleep 0.5; done; \
	  sleep 1; \
	}; \
	askn=0; \
	ask() { \
	  askn=$$((askn % 12 + 1)); \
	  env $$2 ./$(NCCLIENT) 127.0.0.1 $(PT_NPORT) u$$askn p$$askn $(PT_KEY) 0 1884 $$1 >> .pt-cli.log 2>&1; \
	  sleep 1; \
	}; \
	agg_stats() { grep 'PHASE1 STATS' .pt-srv.log | tail -1 | sed -n 's/.*| peertrust \([0-9]*\) peer[^,]*, \([0-9]*\) below the line, \([0-9]*\) skipped.*/\1 \2 \3/p'; }; \
	start_b "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0"; \
	stdbuf -o0 -e0 $(BIN) -C $(PT_CFG) -v > .pt-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(PT_HPORT)/ && break; \
	  sleep 1; \
	done; \
	for i in $$(seq 1 40); do grep -q "advertised card" .pt-b.log && break; sleep 0.5; done; \
	sleep 2; \
	for i in $$(seq 1 40); do grep -q "come Online" .pt-srv.log && break; sleep 0.5; done; \
	grep -q "come Online" .pt-srv.log \
	  && echo "  [ ok ] peer B is online at the cache server" \
	  || { echo "[FAIL] no peer B online: the rig proves nothing"; ok=0; }; \
	ask 0064 "NC_ECMS=3 NC_ALT_SID=00C8 NC_ALT_SID2=012C" 1; \
	sleep 3; \
	for i in $$(seq 1 20); do grep -q "PHASE1 STATS" .pt-srv.log && break; sleep 0.5; done; \
	sleep 1; \
	nd=$$(grep -c "DELIVERED" .pt-cli.log); \
	if [ "$$nd" = "3" ]; then \
	  echo "  [ ok ] phase 1: three services, three honest keys, three deliveries"; \
	else echo "[FAIL] phase 1 delivered $$nd of 3"; ok=0; fi; \
	nl=$$(grep -c "PEER TRUST" .pt-srv.log); \
	s=$$(agg_stats); \
	below=$$(echo $$s | cut -d' ' -f2); \
	if [ "$$nl" = "0" ] && [ "$$below" = "0" ]; then \
	  echo "  [ ok ] the coarse tier watched and said nothing: no crossing, nobody below the line"; \
	else echo "[FAIL] phase 1: $$nl line(s), stats '$$s'"; ok=0; fi; \
	start_b "CP_FRESH_KEY=0 CP_FORGED_FRESH_EVERY=0"; \
	ask 0190 "NC_ECMS=1" 1; \
	ask 0191 "NC_ECMS=1" 1; \
	for i in $$(seq 1 20); do grep -q "CW REUSE PROOF" .pt-srv.log && break; sleep 0.5; done; \
	nre1=$$(grep -c "CW REUSE PROOF" .pt-srv.log); \
	if [ "$$nre1" = "1" ]; then \
	  echo "  [ ok ] phase 2: the fixed key came back for a second service -- one CW-reuse proof"; \
	else echo "[FAIL] reuse proof 1: $$nre1, expected 1"; ok=0; fi; \
	ask 0192 "NC_ECMS=1" 1; \
	nre2=$$(grep -c "CW REUSE PROOF" .pt-srv.log); \
	if [ "$$nre2" = "2" ]; then \
	  echo "  [ ok ] and a third service completes the second proof"; \
	else echo "[FAIL] reuse proof 2: $$nre2, expected 2"; ok=0; fi; \
	for i in $$(seq 1 20); do grep -q "PEER TRUST" .pt-srv.log && break; sleep 0.5; done; \
	sleep 2; \
	nl=$$(grep -c "PEER TRUST" .pt-srv.log); \
	if [ "$$nl" = "1" ]; then \
	  echo "  [ ok ] two proofs on one (peer, caid) cross the coarse line: exactly ONE sentence"; \
	else echo "[FAIL] $$nl PEER TRUST line(s), expected exactly 1"; ok=0; fi; \
	grep -q "2 proof(s)" .pt-srv.log \
	  && echo "  [ ok ] and it names the counts that did it: 2 proof(s), on this caid" \
	  || { echo "[FAIL] the sentence does not carry the proof count"; ok=0; }; \
	grep -q "nothing is disconnected" .pt-srv.log \
	  && echo "  [ ok ] and it says what the brake is NOT: nothing is disconnected, no client touched" \
	  || { echo "[FAIL] the sentence does not disclaim a disconnect"; ok=0; }; \
	s=$$(agg_stats); \
	below=$$(echo $$s | cut -d' ' -f2); \
	if [ "$$below" = "1" ]; then \
	  echo "  [ ok ] the stats line remembers: $$s"; \
	else echo "[FAIL] stats '$$s', expected 1 below the line"; ok=0; fi; \
	nb_mid=$$(grep -c "got request" .pt-b.log); \
	if [ "$$nb_mid" = "3" ]; then \
	  echo "  [ ok ] B answered all three poisoned services: the brake arms on the second OFFER, after the ask"; \
	else echo "[FAIL] B saw $$nb_mid request(s), expected 3"; ok=0; fi; \
	start_a "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0"; \
	nd3a=$$(grep -c "DELIVERED" .pt-cli.log); \
	ask 0193 "NC_ECMS=1" 2; \
	sleep 3; \
	nb_after=$$(grep -c "got request" .pt-b.log); \
	na=$$(grep -c "got request" .pt-a.log); \
	if [ "$$nb_after" = "$$nb_mid" ]; then \
	  echo "  [ ok ] phase 3: B's share of the fan-out is skipped -- its request count froze at $$nb_mid"; \
	else echo "[FAIL] B still asked: $$nb_mid -> $$nb_after"; ok=0; fi; \
	if [ "$$na" -ge 1 ]; then \
	  echo "  [ ok ] and A is asked instead: $$na request(s) answered -- the skip is per-peer, not a blackout"; \
	else echo "[FAIL] peer A got no request"; ok=0; fi; \
	nd3b=$$(grep -c "DELIVERED" .pt-cli.log); \
	if [ "$$nd3b" = "$$((nd3a+1))" ]; then \
	  echo "  [ ok ] the client got its key anyway: one more delivery, from A (skipping a poisoner costs a client nothing)"; \
	else echo "[FAIL] deliveries $$nd3a -> $$nd3b, expected exactly one more -- the brake must never black-screen"; ok=0; fi; \
	s=$$(agg_stats); \
	skipped=$$(echo $$s | cut -d' ' -f3); \
	if [ "$$skipped" -ge 1 ] 2>/dev/null; then \
	  echo "  [ ok ] the stats line carries the brake: $$s"; \
	else echo "[FAIL] stats '$$s', expected skipped >= 1"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv $$peerb $$peera 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peerb $$peera 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 2.9 -- the cache protocol guard (ingress validation), live.
#
# THE SCENARIO. One configured peer A (honest, fresh keys) and one STRANGER S
# (configured nowhere; CP_STRANGER=1 means it never announces itself, and it
# pushes well-formed 29-byte TYPE_REPLYs from the dark).
#   Phase 1: honest traffic only -- three deliveries, the guard is silent,
#   the stats read `cache guard 0 short, 0 unconfigured`.
#   Phase 2: S pushes from the dark. Stock drops these before any layer (that
#   was always true); the guard is what makes it VISIBLE: exactly one
#   "unconfigured" line in the window, counters moving, no delivery from S.
#   Phase 3: A's next first reply is TRUNCATED to 8 bytes (CP_SHORT_REPLY=1) --
#   below TYPE_REPLY's 13-byte minimum. Pre-2.9 the handler read the integrity
#   byte from uninitialised stack memory and decided on it; now the datagram
#   is dropped before any parse, with one line naming the type, both lengths
#   and the sender. A's next reply is intact and delivers -- the gate costs a
#   well-formed sender nothing.
# ---------------------------------------------------------------------------

CG_HPORT = 16640
CG_CACHE = 16641
CG_A     = 16642
CG_S     = 16643
CG_NPORT = 16644
CG_TPORT = 16650
CG_CFG   = .cg.cfg
CG_KEY   = 0102030405060708091011121314
CG_GOOD  = A54DCABC2530BB106D132CACD6237B74

.PHONY: cg
cg: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@{ printf 'HTTP PORT: $(CG_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-cg\nTELNET PORT: $(CG_TPORT)\n'; \
	   printf 'CACHE PORT: $(CG_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(CG_A) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'STATS-WINDOW: 2000\n\n'; \
	   printf '[ cg ]\nCAID: 1884\nPORT: $(CG_NPORT)\n'; \
	   for u in 1 2 3 4 5 6 7 8 9 10 11 12; do printf 'USER: u%d p%d\n' $$u $$u; done; } > $(CG_CFG); \
	ok=1; \
	rm -f .cg-srv.log .cg-a.log .cg-s.log .cg-cli.log; \
	peera=0; peers=0; askn=0; \
	start_a() { \
	  [ "$$peera" != "0" ] && { kill -9 $$peera 2>/dev/null; sleep 0.5; }; \
	  rm -f .cg-a.log; \
	  env $$1 stdbuf -o0 -e0 ./$(CACHEPEER) $(CG_CACHE) $(CG_A) $(CG_GOOD) 32 > .cg-a.log 2>&1 & \
	  peera=$$!; \
	  for i in $$(seq 1 40); do grep -q "advertised card" .cg-a.log && break; sleep 0.5; done; \
	  for i in $$(seq 1 40); do grep -q "Online.*$(CG_A)" .cg-srv.log && break; sleep 0.5; done; \
	  sleep 1; \
	}; \
	ask() { \
	  askn=$$((askn % 12 + 1)); \
	  env $$2 ./$(NCCLIENT) 127.0.0.1 $(CG_NPORT) u$$askn p$$askn $(CG_KEY) 0 1884 $$1 >> .cg-cli.log 2>&1; \
	  sleep 1; \
	}; \
	guard_stats() { grep 'PHASE1 STATS' .cg-srv.log | tail -1 | sed -n 's/.*| cache guard \([0-9]*\) short, \([0-9]*\) unconfigured.*/\1 \2/p'; }; \
	stdbuf -o0 -e0 $(BIN) -C $(CG_CFG) -v > .cg-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(CG_HPORT)/ && break; \
	  sleep 1; \
	done; \
	start_a "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0"; \
	ask 0400 "NC_ECMS=1" ; \
	ask 0401 "NC_ECMS=1" ; \
	ask 0402 "NC_ECMS=1" ; \
	sleep 3; \
	for i in $$(seq 1 20); do grep -q "PHASE1 STATS" .cg-srv.log && break; sleep 0.5; done; \
	nd=$$(grep -c "DELIVERED" .cg-cli.log); \
	if [ "$$nd" = "3" ]; then \
	  echo "  [ ok ] phase 1: three services, three honest deliveries"; \
	else echo "[FAIL] phase 1 delivered $$nd of 3"; ok=0; fi; \
	nl=$$(grep -c "CACHE GUARD" .cg-srv.log); \
	s=$$(guard_stats); \
	if [ "$$nl" = "0" ] && [ "$$s" = "0 0" ]; then \
	  echo "  [ ok ] the guard watched honest traffic and said nothing: cache guard 0 short, 0 unconfigured"; \
	else echo "[FAIL] phase 1: $$nl line(s), stats '$$s'"; ok=0; fi; \
	env CP_STRANGER=1 CP_PUSH_HASH=11223344 CP_PUSH_SID=1234 CP_PUSH_CAID=1884 CP_PUSH_TAG=80 CP_REPUSH_MS=1500 CP_REPUSH_SAME=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(CG_CACHE) $(CG_S) $(CG_GOOD) 64 > .cg-s.log 2>&1 & \
	peers=$$!; \
	for i in $$(seq 1 20); do grep -q "CACHE GUARD.*unconfigured" .cg-srv.log && break; sleep 0.5; done; \
	sleep 3; \
	nl=$$(grep -c "CACHE GUARD.*unconfigured" .cg-srv.log); \
	if [ "$$nl" = "1" ]; then \
	  echo "  [ ok ] phase 2: the stranger's pushes are SEEN now -- exactly one line in the window"; \
	else echo "[FAIL] $$nl unconfigured line(s), expected exactly 1"; ok=0; fi; \
	grep -q "unconfigured senders -- last: a TYPE_REPLY (2) from 127.0.0.1:$(CG_S)" .cg-srv.log \
	  && echo "  [ ok ] and the line names the sender: 127.0.0.1:$(CG_S)" \
	  || { echo "[FAIL] the line does not name the stranger"; ok=0; }; \
	s=$$(guard_stats); \
	u1=$$(echo $$s | cut -d' ' -f2); \
	if [ "$$u1" -ge 1 ] 2>/dev/null; then \
	  echo "  [ ok ] the stats remember: cache guard 0 short, $$u1 unconfigured"; \
	else echo "[FAIL] stats '$$s', expected unconfigured >= 1"; ok=0; fi; \
	nd2=$$(grep -c "DELIVERED" .cg-cli.log); \
	if [ "$$nd2" = "3" ]; then \
	  echo "  [ ok ] and stock's treatment is unchanged: a dark sender still delivers nothing (deliveries stay 3)"; \
	else echo "[FAIL] deliveries moved to $$nd2 without an honest reply"; ok=0; fi; \
	start_a "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0 CP_SHORT_REPLY=1"; \
	ask 0403 "NC_ECMS=1" ; \
	for i in $$(seq 1 20); do grep -q "short TYPE_REPLY" .cg-srv.log && break; sleep 0.5; done; \
	sleep 2; \
	nl=$$(grep -c "short TYPE_REPLY" .cg-srv.log); \
	if [ "$$nl" = "1" ]; then \
	  echo "  [ ok ] phase 3: A's truncated reply is dropped before any parse -- exactly one line"; \
	else echo "[FAIL] $$nl short line(s), expected exactly 1"; ok=0; fi; \
	grep -q "8 bytes, this type needs 13" .cg-srv.log \
	  && echo "  [ ok ] and it names the evidence: 8 bytes against a 13-byte minimum" \
	  || { echo "[FAIL] the line does not carry both lengths"; ok=0; }; \
	s=$$(guard_stats); \
	if [ "$$(echo $$s | cut -d' ' -f1)" = "1" ]; then \
	  echo "  [ ok ] the stats remember: cache guard 1 short, $$(echo $$s | cut -d' ' -f2) unconfigured"; \
	else echo "[FAIL] stats '$$s', expected 1 short"; ok=0; fi; \
	ask 0404 "NC_ECMS=1" ; \
	sleep 2; \
	nd3=$$(grep -c "DELIVERED" .cg-cli.log); \
	if [ "$$nd3" = "4" ]; then \
	  echo "  [ ok ] A's next reply, intact, delivers: the gate costs a well-formed sender nothing"; \
	else echo "[FAIL] deliveries $$nd3, expected 4 -- the guard must never cost a good packet"; ok=0; fi; \
	nl=$$(grep -c "CACHE GUARD" .cg-srv.log); \
	if [ "$$nl" = "2" ]; then \
	  echo "  [ ok ] two events, two lines, no noise: the window kept its word"; \
	else echo "[FAIL] $$nl guard line(s) in total, expected exactly 2"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv $$peera $$peers 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peera $$peers 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 2.10 -- the trust lifecycle, live.
#
# THE SCENARIO. One CSP peer, one newcamd profile, and a state file. Phase 1
# the peer answers honestly twice: standing is written into the fine tier,
# then it goes idle past TRUST-FADE-GRACE (4 s) and the fade relaxes it, and
# the next snapshot writes TF lines to disk. Phase 2 the same peer comes back
# with one fixed poison key: two deliveries, a CW-reuse proof, a negative
# mark, the third offer refused -- and the snapshot now carries an NG line.
# Phase 3 the server is RESTARTED (one garbage line appended to the state file
# first): the loader restores the fine, the coarse and the negative tables,
# counts the bad line and skips it whole, and the very first fresh offer of
# the SAME poison key is refused before any client -- the conviction survived
# the restart that used to launder it.
# ---------------------------------------------------------------------------

TL_HPORT = 16660
TL_CACHE = 16661
TL_A     = 16662
TL_NPORT = 16663
TL_CFG   = .tl.cfg
TL_KEY   = 0102030405060708091011121314
TL_GOOD  = A1B2C316A5A6A7F2A1B2C316A5A6A7F2
TL_POISON= 11223366445566FF77889998AABBCC31
TL_STATE = .tl-trust

.PHONY: tl
tl: $(CACHEPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x '.cachepeer.bin' 2>/dev/null; pkill -9 -x multics 2>/dev/null; \
	  pkill -9 -x '.ncclient.bin' 2>/dev/null; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(TL_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-tl\nTELNET PORT: 16670\n'; \
	   printf 'CACHE PORT: $(TL_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(TL_A) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n'; \
	   printf 'TRUST-PERSIST: ON\n'; \
	   printf 'TRUST-PERSIST-FILE: $(TL_STATE)\n'; \
	   printf 'TRUST-PERSIST-EVERY: 2\n'; \
	   printf 'TRUST-FADE-GRACE: 4\n'; \
	   printf 'TRUST-FADE-STEP: 1\n'; \
	   printf 'STATS-WINDOW: 2000\n\n'; \
	   printf '[ tl ]\nCAID: 1884\nPORT: $(TL_NPORT)\n'; \
	   for u in 1 2 3 4 5 6 7 8 9 10 11 12; do printf 'USER: u%d p%d\n' $$u $$u; done; } > $(TL_CFG); \
	ok=1; \
	rm -f .tl-srv.log .tl-a.log .tl-cli.log $(TL_STATE) $(TL_STATE).tmp; \
	peera=0; askn=0; \
	start_a() { \
	  [ "$$peera" != "0" ] && { kill -9 $$peera 2>/dev/null; sleep 0.5; }; \
	  rm -f .tl-a.log; \
	  env $$1 stdbuf -o0 -e0 ./$(CACHEPEER) $(TL_CACHE) $(TL_A) $$2 32 > .tl-a.log 2>&1 & \
	  peera=$$!; \
	  for i in $$(seq 1 40); do grep -q "advertised card" .tl-a.log 2>/dev/null && break; sleep 0.5; done; \
	  for i in $$(seq 1 40); do grep -q "Online.*$(TL_A)" .tl-srv.log && break; sleep 0.5; done; \
	  sleep 1; \
	}; \
	ask() { \
	  askn=$$((askn % 12 + 1)); \
	  env $$2 ./$(NCCLIENT) 127.0.0.1 $(TL_NPORT) u$$askn p$$askn $(TL_KEY) 0 1884 $$1 >> .tl-cli.log 2>&1; \
	  sleep 1; \
	}; \
	life_stats() { grep 'PHASE1 STATS' .tl-srv.log | tail -1 | sed -n 's/.*| lifecycle \([0-9]*\) fine, \([0-9]*\) coarse, \([0-9]*\) negative restored, \([0-9]*\) bad.*/\1 \2 \3 \4/p'; }; \
	fade_stats() { grep 'PHASE1 STATS' .tl-srv.log | tail -1 | sed -n 's/.*| fade \([0-9]*\), snapshots \([0-9]*\) ok, \([0-9]*\) failed.*/\1 \2 \3/p'; }; \
	stdbuf -o0 -e0 $(BIN) -C $(TL_CFG) -v > .tl-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(TL_HPORT)/ && break; \
	  sleep 1; \
	done; \
	start_a "CP_FRESH_KEY=1 CP_FORGED_FRESH_EVERY=0" $(TL_GOOD); \
	ask 0064 "NC_ECMS=1" ; \
	ask 00C8 "NC_ECMS=1" ; \
	sleep 3; \
	for i in $$(seq 1 20); do grep -q "PHASE1 STATS" .tl-srv.log && break; sleep 0.5; done; \
	nd=$$(grep -c "DELIVERED" .tl-cli.log); \
	if [ "$$nd" = "2" ]; then \
	  echo "  [ ok ] phase 1: two services, two honest deliveries -- the rig is alive"; \
	else echo "[FAIL] phase 1 delivered $$nd of 2"; ok=0; fi; \
	sleep 4; \
	for i in $$(seq 1 20); do grep -q "PHASE1 STATS" .tl-srv.log && break; sleep 0.5; done; \
	f=$$(fade_stats); \
	fsnap=$$(echo $$f | cut -d' ' -f2); \
	if [ "$$fsnap" -ge 1 ] 2>/dev/null; then \
	  echo "  [ ok ] the cache thread is writing snapshots: $$fsnap ok, none failed"; \
	else echo "[FAIL] no snapshot written: fade/snapshots '$$f'"; ok=0; fi; \
	start_a "CP_FRESH_KEY=0 CP_FORGED_FRESH_EVERY=0" $(TL_POISON); \
	NC_ECMS=3 NC_ALT_SID=00CD NC_ALT_SID2=012C NC_ECM_GAP_MS=2500 \
	  ./$(NCCLIENT) 127.0.0.1 $(TL_NPORT) u3 p3 $(TL_KEY) 0 1884 0190 > .tl-cli2.log 2>&1; \
	sleep 3; \
	for i in $$(seq 1 20); do grep -q "CW NEGATIVE MARK" .tl-srv.log && break; sleep 0.5; done; \
	nmark=$$(grep -c "CW NEGATIVE MARK" .tl-srv.log); \
	if [ "$$nmark" = "1" ]; then \
	  echo "  [ ok ] phase 2: the reused poison key is proven and filed (one CW NEGATIVE MARK)"; \
	else echo "[FAIL] $$nmark negative mark line(s), expected exactly 1"; ok=0; fi; \
	nng=$$(grep -c "^NG " $(TL_STATE) 2>/dev/null); \
	if [ "$${nng:-0}" = "1" ] 2>/dev/null; then \
	  echo "  [ ok ] the conviction is on disk: exactly one NG line in the state file"; \
	else echo "[FAIL] $${nng:-0} NG line(s) in $(TL_STATE), expected 1"; ok=0; fi; \
	sleep 6; \
	for i in $$(seq 1 20); do grep -q "PHASE1 STATS" .tl-srv.log && break; sleep 0.5; done; \
	sleep 1; \
	f=$$(fade_stats); \
	faded=$$(echo $$f | cut -d' ' -f1); \
	if [ "$$faded" -ge 1 ] 2>/dev/null; then \
	  echo "  [ ok ] idle past the grace, the fade relaxed the condemned entries: $$faded move(s)"; \
	else echo "[FAIL] fade counter is '$$f' -- nothing faded after the grace"; ok=0; fi; \
	ntf=$$(grep -c "^TF " $(TL_STATE) 2>/dev/null); \
	if [ "$${ntf:-0}" -ge 1 ] 2>/dev/null; then \
	  echo "  [ ok ] the fine tier is on disk too: $$ntf TF line(s)"; \
	else echo "[FAIL] no TF line in $(TL_STATE): the fine tier did not persist"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	echo "THIS LINE IS NOT A RECORD" >> $(TL_STATE); \
	rm -f .tl-srv.log; \
	stdbuf -o0 -e0 $(BIN) -C $(TL_CFG) -v > .tl-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(TL_HPORT)/ && break; \
	  sleep 1; \
	done; \
	sleep 2; \
	for i in $$(seq 1 20); do grep -q "trust lifecycle: loaded" .tl-srv.log && break; sleep 0.5; done; \
	grep -q "trust lifecycle: loaded" .tl-srv.log \
	  && echo "  [ ok ] phase 3: the restart read its state back -- the loader spoke once, at startup" \
	  || { echo "[FAIL] no 'trust lifecycle: loaded' line after the restart"; ok=0; }; \
	grep -E "trust lifecycle: loaded [0-9]+ fine, [0-9]+ coarse, [1-9][0-9]* negative" .tl-srv.log >/dev/null \
	  && echo "  [ ok ] the conviction came back: at least one negative entry restored" \
	  || { echo "[FAIL] the loaded counts show no negative entry"; ok=0; }; \
	grep -q "1 line(s) unusable" .tl-srv.log \
	  && echo "  [ ok ] the garbage line was refused whole and counted, nothing half-applied" \
	  || { echo "[FAIL] the bad line was not counted as unusable"; ok=0; }; \
	s=$$(life_stats); \
	nrest=$$(echo $$s | cut -d' ' -f3); \
	nbad=$$(echo $$s | cut -d' ' -f4); \
	if [ "$$nrest" = "1" ] && [ "$$nbad" = "1" ]; then \
	  echo "  [ ok ] the stats remember both: 1 negative restored, 1 bad line"; \
	else echo "[FAIL] lifecycle stats '$$s', expected 1 restored, 1 bad"; ok=0; fi; \
	kill -9 $$peera 2>/dev/null; sleep 0.5; \
	rm -f .tl-a.log; \
	env CP_FRESH_KEY=0 CP_FORGED_FRESH_EVERY=0 \
	    CP_PUSH_HASH=11223344 CP_PUSH_SID=0190 CP_PUSH_CAID=1884 CP_PUSH_TAG=80 \
	    CP_REPUSH_MS=1500 CP_REPUSH_SAME=1 \
	    stdbuf -o0 -e0 ./$(CACHEPEER) $(TL_CACHE) $(TL_A) $(TL_POISON) 32 > .tl-a.log 2>&1 & \
	peera=$$!; \
	for i in $$(seq 1 20); do grep -q "unsolicited push" .tl-a.log 2>/dev/null && break; sleep 0.5; done; \
	for i in $$(seq 1 30); do grep -q "CW NEGATIVE: " .tl-srv.log && break; sleep 0.5; done; \
	nref2=$$(grep -c "CW NEGATIVE: " .tl-srv.log); \
	if [ "$$nref2" -ge 1 ]; then \
	  echo "  [ ok ] the first fresh offer of the same poison is refused at ingest after the restart"; \
	else echo "[FAIL] $$nref2 refusal line(s) after restart, expected >= 1"; ok=0; fi; \
	grep -q "CW NEGATIVE MARK" .tl-srv.log \
	  && { echo "[FAIL] a second mark was filed -- the restore duplicated the conviction"; ok=0; } \
	  || echo "  [ ok ] and it is the restored conviction biting, not a new one: no second mark"; \
	nd2=$$(grep -c "DELIVERED" .tl-cli2.log); \
	if [ "$$nd2" = "2" ]; then \
	  echo "  [ ok ] deliveries unchanged: the pre-proof two only, the third reached nobody"; \
	else echo "[FAIL] poison deliveries $$nd2, expected 2"; ok=0; fi; \
	f2=$$(fade_stats); \
	ffail=$$(echo $$f2 | cut -d' ' -f3); \
	if [ "$$ffail" = "0" ] 2>/dev/null; then \
	  echo "  [ ok ] no snapshot failure across both runs (fade/snapshots '$$f2')"; \
	else echo "[FAIL] $$ffail snapshot failure(s)"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv $$peera 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peera 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.1 -- the HTTP request surface, live.
#
# THE SCENARIO. Two servers: A with HTTP USER+PASS, B with a password and NO
# user line -- the exact file stock served wide open (the duplicated
# cfg.http.user[0] check never looked at the password). Phase 1 pins the
# honest behaviour of A: correct credentials pass, no and wrong credentials
# get 401. Phase 2 throws the parser probes stock died or corrupted state on:
# a 400-char Basic payload (base64_pdecode had no output bound -- decoded
# past pass[256] on the stack), 30 header lines (hdrcount incremented past
# headers[20]), a 600-byte header value and a 700-byte request line
# (buf2str had no bound), a POST body with 40 &name=value pairs (explode_post
# had no bound past postlist[20]) -- after each probe the server must still
# answer, and the log must stay free of backtraces. Phase 3 pins the closed
# door: B without credentials is 401 now, not the page.
# ---------------------------------------------------------------------------

HA_HPORT = 16680
HA_HPORT2 = 16681
HA_TPORT = 16685
HA_CFG   = .ha.cfg
HA_CFG2  = .ha2.cfg

.PHONY: ha
ha:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x '.cachepeer.bin' 2>/dev/null; pkill -9 -x multics 2>/dev/null; \
	  pkill -9 -x '.ncclient.bin' 2>/dev/null; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(HA_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-ha\nTELNET PORT: $(HA_TPORT)\n\n'; \
	   printf '[ ha ]\nNEWCAMD PORT: 0\n'; } > $(HA_CFG); \
	{ printf 'HTTP PORT: $(HA_HPORT2)\nHTTP PASS: secret\n'; \
	  printf 'HTTP TITLE: mcs-ha2\nTELNET PORT: 16686\n\n'; \
	  printf '[ ha2 ]\nNEWCAMD PORT: 0\n'; } > $(HA_CFG2); \
	ok=1; \
	rm -f .ha-srv.log .ha2-srv.log; \
	stdbuf -o0 -e0 $(BIN) -C $(HA_CFG) -v > .ha-srv.log 2>&1 & \
	srv=$$!; \
	stdbuf -o0 -e0 $(BIN) -C $(HA_CFG2) -v > .ha2-srv.log 2>&1 & \
	srv2=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(HA_HPORT)/ && break; \
	  sleep 1; \
	done; \
	code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HA_HPORT)/); \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] phase 1: the right credentials open the page"; \
	else echo "[FAIL] GET with admin:admin answered '$$code'"; ok=0; fi; \
	code=$$(curl -s -m 3 -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HA_HPORT)/); \
	if [ "$$code" = "401" ]; then \
	  echo "  [ ok ] no credentials: 401"; \
	else echo "[FAIL] GET without credentials answered '$$code', expected 401"; ok=0; fi; \
	code=$$(curl -s -m 3 -u admin:wrong -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HA_HPORT)/); \
	if [ "$$code" = "401" ]; then \
	  echo "  [ ok ] wrong credentials: 401"; \
	else echo "[FAIL] GET with admin:wrong answered '$$code', expected 401"; ok=0; fi; \
	longb64=$$(printf 'A%.0s' $$(seq 1 400)); \
	code=$$(curl -s -m 3 -o /dev/null -w '%{http_code}' -H "Authorization: Basic $$longb64" http://127.0.0.1:$(HA_HPORT)/); \
	if [ "$$code" = "401" ]; then \
	  echo "  [ ok ] a 400-char Basic payload is refused without decoding (pass[256] stays pass[256])"; \
	else echo "[FAIL] long Basic payload answered '$$code', expected 401"; ok=0; fi; \
	hdrs=""; \
	for i in $$(seq 1 30); do hdrs="$$hdrs -H X-$$i:v"; done; \
	code=$$(curl -s -m 3 -o /dev/null -w '%{http_code}' $$hdrs http://127.0.0.1:$(HA_HPORT)/); \
	if [ "$$code" = "401" ]; then \
	  echo "  [ ok ] 30 header lines: the parser stops at 20, the request is still just unauthorised"; \
	else echo "[FAIL] 30-header request answered '$$code', expected 401"; ok=0; fi; \
	bigval=$$(printf 'A%.0s' $$(seq 1 600)); \
	code=$$(curl -s -m 3 -o /dev/null -w '%{http_code}' -H "Cookie: $$bigval" http://127.0.0.1:$(HA_HPORT)/); \
	if [ "$$code" = "401" ]; then \
	  echo "  [ ok ] a 600-byte header value is truncated at the field, not past it"; \
	else echo "[FAIL] long header value answered '$$code', expected 401"; ok=0; fi; \
	longpath=$$(printf 'A%.0s' $$(seq 1 700)); \
	curl -s -m 3 -o /dev/null "http://127.0.0.1:$(HA_HPORT)/$$longpath"; \
	code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HA_HPORT)/); \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] a 700-byte request line wrote nothing past path[512], and the server still serves"; \
	else echo "[FAIL] server unhealthy after the long request line ('$$code')"; ok=0; fi; \
	body=$$; \
	body=""; \
	for i in $$(seq 1 40); do body="$$body n$$i=v$$i &"; done; \
	code=$$(curl -s -m 3 -o /dev/null -w '%{http_code}' --data "$$body" http://127.0.0.1:$(HA_HPORT)/); \
	if [ "$$code" = "401" ]; then \
	  echo "  [ ok ] a POST body with 40 pairs stops at postlist's 20, still 401"; \
	else echo "[FAIL] 40-pair POST answered '$$code', expected 401"; ok=0; fi; \
	code=$$(curl -s -m 3 -o /dev/null -w '%{http_code}' http://127.0.0.1:$(HA_HPORT2)/); \
	if [ "$$code" = "401" ]; then \
	  echo "  [ ok ] phase 3: the pass-only file that stock served wide open answers 401 now"; \
	else echo "[FAIL] pass-only server answered '$$code' WITHOUT credentials -- the door is still open"; ok=0; fi; \
	code=$$(curl -s -m 3 -o /dev/null -w '%{http_code}' -H "Authorization: Basic $$longb64" http://127.0.0.1:$(HA_HPORT2)/); \
	if [ "$$code" = "401" ]; then \
	  echo "  [ ok ] and the same parser probes get the same refusals on it"; \
	else echo "[FAIL] pass-only long Basic answered '$$code', expected 401"; ok=0; fi; \
	if grep -q "Segmentation\|backtrace" .ha-srv.log .ha2-srv.log 2>/dev/null; then \
	  echo "[FAIL] a backtrace in a server log: something crashed during the probes"; ok=0; \
	else echo "  [ ok ] both servers' logs are clean: no crash, no backtrace, all probes survived"; fi; \
	if kill -0 $$srv 2>/dev/null && kill -0 $$srv2 2>/dev/null; then \
	  echo "  [ ok ] both servers survived"; \
	else echo "[FAIL] a server died"; ok=0; fi; \
	kill $$srv $$srv2 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$srv2 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.2 -- client names are data, not markup, live.
#
# THE SCENARIO. A newcamd profile whose username IS an HTML injection:
# <script>alert(1)</script>. Upstream wrote client names into the web
# interface raw: the index/newcamd HTML rows had no xmlescape (only the XML
# branches did), the detail pages and the debug labels wrote straight into
# the buffer. A real CCcam peer even controls its own realname remotely.
# The fixed binary must render the name inert everywhere a page shows it,
# and serve honestly otherwise.
# ---------------------------------------------------------------------------

XS_HPORT = 16690
XS_NPORT = 16691
XS_CFG   = .xs.cfg
XS_KEY   = 0102030405060708091011121314

.PHONY: xs
xs:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x '.cachepeer.bin' 2>/dev/null; pkill -9 -x multics 2>/dev/null; \
	  pkill -9 -x '.ncclient.bin' 2>/dev/null; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(XS_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-xs\n\n'; \
	   printf '[ xs ]\nCAID: 1884\nPORT: $(XS_NPORT)\n'; \
	   printf 'USER: <script>alert(1)</script> secret\n'; \
	   for u in 2 3; do printf 'USER: u%d p%d\n' $$u $$u; done; } > $(XS_CFG); \
	ok=1; \
	rm -f .xs-srv.log .xs-cli.log; \
	stdbuf -o0 -e0 $(BIN) -C $(XS_CFG) -v > .xs-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(XS_HPORT)/ && break; \
	  sleep 1; \
	done; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(XS_NPORT) '<script>alert(1)</script>' secret $(XS_KEY) 0 1884 0064 >> .xs-cli.log 2>&1; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(XS_NPORT) u2 p2 $(XS_KEY) 0 1884 00C8 >> .xs-cli.log 2>&1; \
	sleep 3; \
	np=0; ne=0; \
	for pg in / /newcamd /newcamdclient?id=1 /debug; do \
	  body=$$(curl -s -m 3 -u admin:admin "http://127.0.0.1:$(XS_HPORT)$$pg"); \
	  if echo "$$body" | grep -q '<script>'; then \
	    echo "[FAIL] $$pg carries a RAW <script> tag from the username"; ok=0; np=$$((np+1)); \
	  fi; \
	  if echo "$$body" | grep -q '&lt;script&gt;'; then ne=$$((ne+1)); fi; \
	done; \
	if [ "$$np" = "0" ]; then \
	  echo "  [ ok ] no page carries the raw username markup (index, newcamd, client detail, debug)"; \
	fi; \
	if [ "$$ne" -ge 2 ]; then \
	  echo "  [ ok ] the escaped form is what the pages render: $$ne page(s) show &lt;script&gt;"; \
	else echo "[FAIL] only $$ne page(s) show the escaped name -- the name vanished or stayed raw"; ok=0; fi; \
	code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(XS_HPORT)/); \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] the interface still serves: index answered 200"; \
	else echo "[FAIL] index answered '$$code'"; ok=0; fi; \
	nlogin=$$(grep -c "connected" .xs-srv.log); \
	if [ "$$nlogin" -ge 1 ]; then \
	  echo "  [ ok ] the poisoned name logged in fine (the fix is at render, not at login)"; \
	else echo "[FAIL] the poisoned username did not connect ($$nlogin)"; ok=0; fi; \
	if grep -q "Segmentation\|backtrace" .xs-srv.log 2>/dev/null; then \
	  echo "[FAIL] a backtrace in the server log"; ok=0; \
	else echo "  [ ok ] the server log is clean"; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.3 -- the logging ring never overflows, live.
#
# THE SCENARIO. The defect: debugf() vsprintf'd into debugline[1024] and
# add_dbgline() strcpy'd the result into dbgline[..][512] -- a caller line
# longer than the slot wrote past the ring in .bss. The live probe drives
# the widest legal line: a newcamd username of 62 'A's (the wire accepts
# <=63; config stores it in a 64-byte field) logs in and every layer
# prints it -- connect, ECM, decode-fail. /debug must render those lines
# escaped (3.2's printer) with the name whole but the LINE bounded, and
# the server must not care. The probe pair: the same server also accepts
# an overlong 900-char login attempt and refuses it by LENGTH -- upstream's
# own guard -- while the ring stays healthy.
# ---------------------------------------------------------------------------

DL_HPORT = 16700
DL_NPORT = 16701
DL_CFG   = .dl.cfg
DL_KEY   = 0102030405060708091011121314

.PHONY: dl
dl:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x '.cachepeer.bin' 2>/dev/null; pkill -9 -x multics 2>/dev/null; \
	  pkill -9 -x '.ncclient.bin' 2>/dev/null; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(DL_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-dl\n\n'; \
	   printf '[ dl ]\nCAID: 1884\nPORT: $(DL_NPORT)\n'; \
	   printf 'USER: %s pw\n' "$$(printf 'A%.0s' $$(seq 1 62))"; \
	   printf 'USER: u2 p2\n'; } > $(DL_CFG); \
	ok=1; \
	rm -f .dl-srv.log .dl-cli.log .dl-debug.html; \
	stdbuf -o0 -e0 $(BIN) -C $(DL_CFG) -v > .dl-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  curl -s -m 3 -u admin:admin -o /dev/null http://127.0.0.1:$(DL_HPORT)/ && break; \
	  sleep 1; \
	done; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(DL_NPORT) "$$(printf 'A%.0s' $$(seq 1 62))" pw $(DL_KEY) 0 1884 0064 >> .dl-cli.log 2>&1; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(DL_NPORT) u2 p2 $(DL_KEY) 0 1884 00C8 >> .dl-cli.log 2>&1; \
	sleep 3; \
	nconn=$$(grep -c "client 'A" .dl-srv.log); \
	if [ "$$nconn" -ge 2 ]; then \
	  echo "  [ ok ] the 62-char name flowed through the layers: $$nconn ring line(s) name it"; \
	else echo "[FAIL] only $$nconn line(s) name the long client"; ok=0; fi; \
	curl -s -m 5 -u admin:admin http://127.0.0.1:$(DL_HPORT)/debug > .dl-debug.html; \
	nesc=$$(grep -c '&#39;A' .dl-debug.html); \
	if [ "$$nesc" -ge 1 ]; then \
	  echo "  [ ok ] /debug renders the named lines escaped: $$nesc occurrence(s)"; \
	else echo "[FAIL] the /debug page does not show the escaped name"; ok=0; fi; \
	nrun=$$(grep -oE 'A\{63,\}' .dl-debug.html | head -1 | awk '{print length}'); \
	if [ "$${nrun:-0}" = "0" ] || [ "$$nrun" -le 63 ] 2>/dev/null; then \
	  echo "  [ ok ] the name appears whole (max legal), the LINE bounded: longest run $$nrun"; \
	else echo "[FAIL] an A-run of $$nrun bytes: the line overflowed its slot"; ok=0; fi; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(DL_NPORT) "$$(printf 'B%.0s' $$(seq 1 900))" pw $(DL_KEY) 0 1884 0064 >> .dl-cli.log 2>&1; \
	sleep 2; \
	nunk=$$(grep -c "unknown user 'B" .dl-srv.log); \
	nbmax=$$(grep -oE "B\{80,\}" .dl-srv.log | head -1 | awk '{print length}'); \
	if [ "$$nunk" -ge 1 ] && [ "$${nbmax:-0}" = "0" ] || [ "$${nbmax:-0}" -le 63 ] 2>/dev/null; then \
	  echo "  [ ok ] a 900-char login attempt is refused as unknown user, and the refusal line itself is clamped (longest B-run: $${nbmax:-0})"; \
	else echo "[FAIL] the overlong refusal is not clamped: line(s)=$$nunk run=$$nbmax"; ok=0; fi; \
	code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(DL_HPORT)/); \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] the interface still serves"; \
	else echo "[FAIL] index answered '$$code'"; ok=0; fi; \
	if grep -q "Segmentation\|backtrace" .dl-srv.log 2>/dev/null; then \
	  echo "[FAIL] a backtrace in the server log"; ok=0; \
	else echo "  [ ok ] the server log is clean"; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.4 -- the cache allocation never crashes the server, live.
#
# THE DEFECT. getcachetabbycaid() allocated the per-CAID cache table and
# cache_new()/cache_setdcw()/PIPE_CACHE_REPLY allocated their nodes with no
# NULL checks, and every caller dereferenced the result. One failed
# allocation -- one -- was a SIGSEGV on the cache thread, which is the
# process. Bitten pre-fix: LD_PRELOAD refusing exactly the table allocation
# (calloc(90112): 88-byte packed cache_data x 1024 slots, transformed by GCC
# from the malloc+memset pair; ca-bite.c) killed the 3.3 binary with exit 139
# on the first honest client ECM.
#
# THE FIX. Fourteen guards, one per allocation and one per caller that
# dereferences what an allocation returned: the table allocator says so once
# per failed attempt and returns NULL; every caller drops the one event it
# was holding (a find, a request, a reply, one peer report) and the ECM's own
# cachetimeout fallback takes over. Nothing blocks, nothing queues (GR9), no
# pool, no layout change.
#
# THE SCENARIO. Phase 1 runs the fixed binary with the same bite active and
# drives two client ECMs: the server must survive, say so in bounded lines,
# and keep serving. Phase 2 restarts clean, with a real cache peer pushing a
# real CW: the whole cache path -- table allocation, entry, delivery -- must
# work exactly as before, because guards that broke the happy path would be
# worse than the crash.
#
# PORTS: 16710 http, 16711 newcamd, 16712 cache, 16713 peer.
# Distinct from `dl` (16700/16701) and everything below 16700.
# ---------------------------------------------------------------------------

CA_HPORT  = 16710
CA_NPORT  = 16711
CA_CACHE  = 16712
CA_PEER   = 16713
CA_CFG    = .ca.cfg
CA_KEY    = 0102030405060708091011121314
CA_CW     = 11223366445566FF77889998AABBCC31
CACHEPEER = .cachepeer.bin
CABITE    = .ca-bite.so

.PHONY: ca

$(CABITE): ca-bite.c
	$(CC) -O2 -std=gnu89 -Wall -shared -fPIC -o $@ ca-bite.c -ldl
	@chmod +x $@   # a restored workspace can lose the exec bit; the recipe must not depend on it

ca: $(CACHEPEER) $(NCCLIENT) $(CABITE)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x '.cachepeer.bin' 2>/dev/null; pkill -9 -x multics 2>/dev/null; \
	  pkill -9 -x '.ncclient.bin' 2>/dev/null; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(CA_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-ca\n'; \
	   printf 'CACHE PORT: $(CA_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(CA_PEER) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\n\n'; \
	   printf '[ cafix ]\nCAID: 1884\nPORT: $(CA_NPORT)\nUSER: u1 p1\nUSER: u2 p2\n'; } > $(CA_CFG); \
	ok=1; \
	rm -f .ca-srv1.log .ca-srv2.log .ca-peer.log .ca-cli1.log .ca-cli2.log .ca-cli3.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(CA_CACHE) $(CA_PEER) $(CA_CW) 1 > .ca-peer.log 2>&1 & \
	peer=$$!; \
	sleep 1; \
	LD_PRELOAD=$$PWD/$(CABITE) stdbuf -o0 -e0 $(BIN) -C $(CA_CFG) -v > .ca-srv1.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CA_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CA_NPORT) u1 p1 $(CA_KEY) 0 1884 0064 > .ca-cli1.log 2>&1; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CA_NPORT) u2 p2 $(CA_KEY) 0 1884 00C8 > .ca-cli2.log 2>&1; \
	sleep 3; \
	if kill -0 $$srv 2>/dev/null; then \
	  echo "  [ ok ] the server SURVIVED the refused cache-table allocation (pre-fix: SIGSEGV on the first ECM)"; \
	else echo "[FAIL] the server died with the table allocation refused"; ok=0; fi; \
	noom=$$(grep -c "CACHE: out of memory" .ca-srv1.log); \
	if [ "$$noom" -ge 4 ]; then \
	  echo "  [ ok ] every failed attempt said so, once, bounded: $$noom line(s) for two ECMs"; \
	else echo "[FAIL] the allocation failures were silent ($$noom lines)"; ok=0; fi; \
	nbite=$$(grep -c "ca-bite.*refused" .ca-srv1.log); \
	if [ "$$nbite" -ge 4 ]; then \
	  echo "  [ ok ] the bite really fired: $$nbite table allocation(s) refused"; \
	else echo "[FAIL] the bite never refused an allocation ($$nbite) -- the probe proved nothing"; ok=0; fi; \
	code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CA_HPORT)/); \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] the interface still serves with the cache table missing"; \
	else echo "[FAIL] index answered '$$code' with the cache table missing"; ok=0; fi; \
	if grep -q "Segmentation\|backtrace" .ca-srv1.log 2>/dev/null; then \
	  echo "[FAIL] a backtrace in the phase-1 server log"; ok=0; \
	else echo "  [ ok ] the phase-1 server log is clean"; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	sleep 2; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(CA_CACHE) $(CA_PEER) $(CA_CW) 1 > .ca-peer.log 2>&1 & \
	peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(CA_CFG) -v > .ca-srv2.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CA_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in $$(seq 1 30); do grep -q "advertised card" .ca-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	./$(NCCLIENT) 127.0.0.1 $(CA_NPORT) u1 p1 $(CA_KEY) 0 1884 0064 > .ca-cli3.log 2>&1; \
	sleep 1; \
	if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	ndcw=$$(grep -c "DELIVERED A CONTROL WORD" .ca-cli3.log); \
	if [ "$$ndcw" -ge 1 ]; then \
	  echo "  [ ok ] phase 2: a real cache peer's CW was delivered to a real client (the guards did not touch the happy path)"; \
	else echo "[FAIL] phase 2 delivered no CW -- the guards broke the normal path"; ok=0; fi; \
	grep -q "dcw=$(CA_CW)" .ca-cli3.log \
	  && echo "  [ ok ] and it is exactly the CW the peer pushed" \
	  || { echo "[FAIL] the delivered CW is not the one the peer pushed"; ok=0; }; \
	grep -q "come Online" .ca-srv2.log \
	  && echo "  [ ok ] the fresh table was allocated and the peer came online" \
	  || { echo "[FAIL] the server never accepted the cache peer on the clean run"; ok=0; }; \
	if [ "$$survived" = "1" ]; then echo "  [ ok ] the clean run survived the whole sequence"; \
	else echo "[FAIL] the clean-run server died"; ok=0; fi; \
	if grep -q "Segmentation\|backtrace" .ca-srv2.log 2>/dev/null; then \
	  echo "[FAIL] a backtrace in the phase-2 server log"; ok=0; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.5 -- a config USER line cannot corrupt the client record, live.
#
# THE DEFECT. The newcamd USER branch parse_str'd the name and the password
# straight into cs_client_data.user[64] / .pass[64] -- adjacent fields of a
# packed struct -- while parse_str() only clamps at 255. A name over 63
# chars spilled into pass (the stored "name" became the 63 chars plus the
# password's own bytes); a password over 63 chars wrote straight over
# userhash. Both faces end the same way, silently: the entry can never log
# in again. Bitten pre-fix, live: `USER: u4 P*70` answered `unknown user
# 'u4'` to u4's own configured password -- the hash had been destroyed at
# parse time.
#
# THE FIX. One hunk in config.c: parse into the scratch buffer, bound each
# copy at its own field edge, one config-parse warning per truncated field
# (config line and column named). The truncated pair stays a WORKING
# credential -- the newcamd wire guard accepts <=63.
#
# THE SCENARIO. A profile with three USER lines: a 70-char name, a control
# pair, and a 70-char password. After the fix: exactly two warnings, and
# all three entries log in -- the two long ones through their truncated
# forms (63 chars of the name / of the password). A 70-char password
# attempt against the truncated store is refused at the password stage
# (honest), and the pre-fix failure signature -- `unknown user` for a
# configured user -- is gone entirely.
#
# PORTS: 16720 http, 16721 newcamd. Distinct from dl (16700/16701) and ca
# (16710-16713).
# ---------------------------------------------------------------------------

CU_HPORT = 16720
CU_NPORT = 16721
CU_CFG   = .cu.cfg
CU_KEY   = 0102030405060708091011121314

.PHONY: cu

cu: $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x multics 2>/dev/null; pkill -9 -x '.ncclient.bin' 2>/dev/null; chmod +x .ncclient.bin 2>/dev/null; sleep 1; true
	@A63=$$(printf 'A%.0s' $$(seq 1 63)); P63=$$(printf 'P%.0s' $$(seq 1 63)); \
	A70=$$(printf 'A%.0s' $$(seq 1 70)); P70=$$(printf 'P%.0s' $$(seq 1 70)); \
	{ printf 'HTTP PORT: $(CU_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-cu\n\n'; \
	   printf '[ cutest ]\nCAID: 1884\nPORT: $(CU_NPORT)\n'; \
	   printf 'USER: %s pw\n' "$$A70"; \
	   printf 'USER: u3 p3\n'; \
	   printf 'USER: u4 %s\n' "$$P70"; } > $(CU_CFG); \
	ok=1; \
	rm -f .cu-srv.log; \
	stdbuf -o0 -e0 $(BIN) -C $(CU_CFG) -v > .cu-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CU_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CU_NPORT) u3 p3 $(CU_KEY) 0 1884 0064 >/dev/null 2>&1; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CU_NPORT) "$$A63" pw $(CU_KEY) 0 1884 0064 >/dev/null 2>&1; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CU_NPORT) u4 "$$P63" $(CU_KEY) 0 1884 0064 >/dev/null 2>&1; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CU_NPORT) u4 "$$P70" $(CU_KEY) 0 1884 0064 >/dev/null 2>&1; \
	sleep 2; \
	nw=$$(grep -c "truncated to 63" .cu-srv.log); \
	if [ "$$nw" = "2" ] && grep -q "USER name too long" .cu-srv.log && grep -q "USER password too long" .cu-srv.log; then \
	  echo "  [ ok ] exactly two parse warnings, one per truncated field, naming the config lines"; \
	else echo "[FAIL] expected 2 truncation warnings (name+password), got $$nw"; ok=0; fi; \
	if grep -qF "client 'u3' connected" .cu-srv.log; then \
	  echo "  [ ok ] the control pair logs in untouched"; \
	else echo "[FAIL] the control pair u3/p3 did not connect"; ok=0; fi; \
	if grep -qF "client '$$A63' connected" .cu-srv.log; then \
	  echo "  [ ok ] the 70-char name entry logs in through its truncated 63-char form (pre-fix: the stored name was mangled with the password's bytes)"; \
	else echo "[FAIL] the truncated 63-char name + pw did not connect"; ok=0; fi; \
	if grep -qF "client 'u4' connected" .cu-srv.log; then \
	  echo "  [ ok ] the 70-char password entry logs in through its truncated form (pre-fix: userhash was destroyed, 'unknown user' for its own pass)"; \
	else echo "[FAIL] u4 + truncated password did not connect"; ok=0; fi; \
	nunk=$$(grep -c "unknown user" .cu-srv.log); \
	if [ "$$nunk" = "0" ]; then \
	  echo "  [ ok ] the pre-fix failure signature is gone: no configured user is 'unknown'"; \
	else echo "[FAIL] $$nunk 'unknown user' line(s) -- a configured user is still unreachable"; ok=0; fi; \
	if grep -q "Segmentation\|backtrace" .cu-srv.log 2>/dev/null; then \
	  echo "[FAIL] a backtrace in the server log"; ok=0; \
	else echo "  [ ok ] the server log is clean"; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] server survived"; else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.6 -- a cache entry's push arm dies with its waiter, live.
#
# THE DEFECT. CACHE_FLAG_SENDPIPE arms a cache entry's push path: the local
# FIND/REQUEST handlers set it while a local ECM is waiting on the cache pipe
# (pcache->ecm), and every push -- cache_setdcw()'s forced push, the FIND
# scans, the cross-channel goodcw walks -- is gated on it, as is the
# TYPE_REQUEST auto-answer towards peers. Nothing in the tree ever cleared
# it. One FIND on a hash, and for the entry's whole alive time (45 s by
# default):
#   - every peer key pushed for that hash is force-marked DCW_SENT and piped
#     at a waiter that is no longer there (the delivery skips DCW_SENT nodes
#     forever after, so the key can never serve anyone again), and
#   - peers asking for the hash get silence (the auto-answer gate reads the
#     stuck arm).
# A cache-heavy deployment with no card for the channel black-screens peers
# for up to 45 s per hash, per event.
#
# THE FIX. The two death moments of the waiter release the arm:
#   cache_clear_sendpipe() -- fetch the entry, clear the flag only when the
#   entry still points at THIS ecm -- called with the death already committed
#   from ecm_setdcwdata() (SUCCESS, any source) and ecm_faileddcw() (FAILED).
#   The FIND/REQUEST handlers refuse to arm a waiter that is already answered
#   or failed, so a death that races the pipe cannot leave a stuck arm
#   behind. Never cleared earlier: while the waiter lives (WAITCACHE or WAIT,
#   servers racing) the arm IS the delivery path.
#
# THE SCENARIO. One server, one cache-only profile, one silent peer.
#   Phase 1  client u1 asks; the ECM reaches the cache REQUEST phase (the
#            peer records it and stays silent) and dies -- no card, no peer.
#   Phase 2  the peer pushes a valid CW 1.2 s after its first request. This
#            is after the waiter's death: post-fix it is stored unmarked;
#            pre-fix the stuck arm force-marks it DCW_SENT.
#   Phase 3  the peer asks the server for the same hash six times. Post-fix
#            the auto-answer replies with the stored key; pre-fix: silence.
#   Phase 4  after the dead ECM has left the ECM store (15 s) but while the
#            cache entry is still alive (45 s), client u2 asks for the SAME
#            ECM. Post-fix the FIND scan serves the stored key instantly;
#            pre-fix the key is DCW_SENT-blocked and u2 decode-fails.
#
# PORTS: 16730 http, 16731 newcamd, 16732 cache, 16733 peer. Distinct from
# dl (16700/01), ca (16710-13), cu (16720/21).
# ---------------------------------------------------------------------------

SP_HPORT = 16730
SP_NPORT = 16731
SP_CACHE = 16732
SP_PEER  = 16733
SP_CFG   = .sp.cfg
SP_KEY   = 0102030405060708091011121314
SP_CW    = 12223367445566FF77889998AABBCC31
SPPEER   = .sp-peer.bin

.PHONY: sp

$(SPPEER): sp-peer.c
	$(CC) -O2 -m64 -std=gnu89 -Wall -Wextra -o $@ sp-peer.c
	@chmod +x $@

sp: $(SPPEER) $(NCCLIENT)
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x '.sp-peer.bin' 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; \
	  pkill -9 -x multics 2>/dev/null; pkill -9 -x '.ncclient.bin' 2>/dev/null; chmod +x .sp-peer.bin .cachepeer.bin .ncclient.bin 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(SP_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-sp\n'; \
	   printf 'CACHE PORT: $(SP_CACHE)\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(SP_PEER) { csp=1 }\n'; \
	   printf 'CACHE FILTER: OFF\nCACHE THRESHOLD: 1\nCACHE FORWARD: ON\n\n'; \
	   printf '[ sp ]\nCAID: 1884\nPORT: $(SP_NPORT)\n'; \
	   printf 'DCW TIMEOUT: 1500\nCACHE TIMEOUT: 0\n'; \
	   printf 'USER: u1 p1\nUSER: u2 p2\n'; } > $(SP_CFG); \
	ok=1; \
	rm -f .sp-srv.log .sp-peer.log .sp-cli1.log .sp-cli2.log; \
	stdbuf -o0 -e0 ./$(SPPEER) $(SP_CACHE) $(SP_PEER) $(SP_CW) > .sp-peer.log 2>&1 & \
	peer=$$!; \
	stdbuf -o0 -e0 $(BIN) -C $(SP_CFG) -v > .sp-srv.log 2>&1 & \
	srv=$$!; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(SP_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in $$(seq 1 30); do grep -q "come Online" .sp-srv.log && break; sleep 0.5; done; \
	sleep 1; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(SP_NPORT) u1 p1 $(SP_KEY) 0 1884 0064 > .sp-cli1.log 2>&1; \
	for i in $$(seq 1 16); do grep -q "SP: PUSH" .sp-peer.log && break; sleep 0.5; done; \
	for i in $$(seq 1 20); do grep -q "SP: DONE" .sp-peer.log && break; sleep 0.5; done; \
	sleep 10; \
	env NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(SP_NPORT) u2 p2 $(SP_KEY) 0 1884 0064 > .sp-cli2.log 2>&1; \
	if kill -0 $$srv 2>/dev/null; then survived=1; else survived=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	grep -q "SP: REQ " .sp-peer.log \
	  && echo "  [ ok ] phase 1: the peer recorded the server's request for the hash" \
	  || { echo "[FAIL] the peer never saw the server's request -- the ECM never reached the cache pipe"; ok=0; }; \
	grep -q "decode-failed (all-zero)" .sp-cli1.log \
	  && echo "  [ ok ] phase 1: the first waiter died (cache-only profile, silent peer)" \
	  || { echo "[FAIL] the first ECM did not fail as designed"; ok=0; }; \
	nans=$$(grep -c "SP: ANSWERED" .sp-peer.log); \
	if [ "$$nans" -ge 5 ]; then \
	  echo "  [ ok ] phase 3: the stored key answers peer requests ($$nans/6) -- the auto-answer gate is open"; \
	else echo "[FAIL] peer requests answered $$nans/6 -- the entry is still silent after its waiter died"; ok=0; fi; \
	grep -q "THE SERVER DELIVERED A CONTROL WORD" .sp-cli2.log \
	  && echo "  [ ok ] phase 4: the SAME ECM from a second client is served from the stored key" \
	  || { echo "[FAIL] the second client got nothing -- the stored key is buried under DCW_SENT"; ok=0; }; \
	grep -q "dcw=$(SP_CW)" .sp-cli2.log \
	  && echo "  [ ok ] and it is exactly the key the peer pushed" \
	  || { echo "[FAIL] the delivered key is not the pushed one"; ok=0; }; \
	if [ "$$survived" = "1" ]; then echo "  [ ok ] the server survived the whole sequence"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	if grep -q "Segmentation\|backtrace" .sp-srv.log 2>/dev/null; then \
	  echo "[FAIL] a backtrace in the server log"; ok=0; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.7 -- the config parsers keep their terminator inside the buffer, live.
#
# THE DEFECT. All six parse_* token readers in parser.c clamped the copy at
# 255 and then wrote str[len]=0 -- so a token of 255+ chars wrote its
# terminator one byte PAST a char[255] buffer. The config layer feeds real
# char[255] buffers: read_config's scratch, parse_server_data's,
# twin_read_chninfo's, and parse_boolean() owns one itself. Bitten pre-fix on
# the REAL server under ASan (detect_leaks=0, halt_on_error=0, recover
# instrumentation): one config with three 255-char tokens produced eight
# "WRITE of size 1" stack-buffer-overflow reports -- parse_value via
# parse_boolean (parser.c:80), parse_str via read_config's USER branch
# (parser.c:96), and the SERVER-line path -- every one a stray zero byte on
# the config thread's stack. In a production build the write is usually
# silent (adjacent padding) and occasionally fatal (adjacent pointer/int);
# ASan makes the always-existing write visible on every hit.
#
# THE FIX (parser.c, TASK 3.7): the clamp is 254, so the terminator lands at
# str[254] -- inside every caller's char[255], still NUL-terminated. Tokens
# of 255+ chars truncate one char earlier; nothing else changes (254-char
# tokens were and are complete).
#
# THE SCENARIO. The same three-token config, on the real server, under the
# same ASan build this target keeps compiled: post-fix the server must boot,
# answer HTTP, have parsed the profile, and log ZERO AddressSanitizer
# reports. The plain -O2 binary must boot the identical config too. The
# unit-level canary proof (36 asserts, six functions, offsets 254/255/256)
# lives in make-x64/test_parser.c.
#
# PORTS: 16740 http, 16741 newcamd. Distinct from sp (16730-33), cu
# (16720/21), ca (16710-13), dl (16700/01).
# ---------------------------------------------------------------------------

PS_HPORT = 16740
PS_NPORT = 16741
PS_CFG   = .ps-asan.cfg
PS_ASRV  = ../make-x64/x64/multics-asan

.PHONY: ps

ps:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@command -v curl >/dev/null || { echo "curl required"; exit 1; }
	@make -C ../make-x64 x64/multics-asan >/dev/null 2>&1 || { echo "asan build failed"; exit 1; }
	@pkill -9 -x multics-asan 2>/dev/null; pkill -9 -x multics 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(PS_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP TITLE: mcs-ps\n'; \
	   printf 'TRUSTED-CACHE-FIRST: '; printf 'X%.0s' $$(seq 1 255); printf '\n'; \
	   printf 'SERVER '; printf 'S%.0s' $$(seq 1 255); printf ' desc 127.0.0.1 1 u p 0\n\n'; \
	   printf '[ psasan ]\nCAID: 1884\nPORT: $(PS_NPORT)\nUSER: '; \
	   printf 'U%.0s' $$(seq 1 255); printf ' p1\n'; } > $(PS_CFG); \
	ok=1; \
	rm -f .ps-asan.log .ps-plain.log; \
	libasan=$$(gcc -print-file-name=libasan.so); \
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 LD_PRELOAD="$$libasan:/usr/bin/libstdbuf.so" stdbuf -o0 -e0 $(PS_ASRV) -C $(PS_CFG) -v > .ps-asan.log 2>&1 & \
	srv=$$!; \
	code=000; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(PS_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] the real server boots under ASan with three 255-char config tokens"; \
	else echo "[FAIL] ASan server answered '$$code'"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then \
	  echo "  [ ok ] the server is alive after the parse"; \
	else echo "[FAIL] the ASan server died"; ok=0; fi; \
	nrep=$$(grep -c "AddressSanitizer" .ps-asan.log); \
	if [ "$$nrep" = "0" ]; then \
	  echo "  [ ok ] zero AddressSanitizer reports (pre-fix: 8 stack-buffer-overflow WRITEs)"; \
	else echo "[FAIL] $$nrep AddressSanitizer report(s) -- the terminator escaped again"; \
	  grep -m2 -A2 "WRITE of size" .ps-asan.log; ok=0; fi; \
	if grep -q "Newcamd Server started on port $(PS_NPORT)" .ps-asan.log; then \
	  echo "  [ ok ] the profile parsed to the end: newcamd port came up"; \
	else echo "[FAIL] the profile did not finish parsing"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	stdbuf -o0 -e0 $(BIN) -C $(PS_CFG) -v > .ps-plain.log 2>&1 & \
	srv=$$!; \
	code=000; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(PS_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] the plain -O2 binary boots the identical config"; \
	else echo "[FAIL] plain binary answered '$$code'"; ok=0; fi; \
	if grep -q "Newcamd Server started on port $(PS_NPORT)" .ps-plain.log; then \
	  echo "  [ ok ] and the plain binary parsed the profile to the end too"; \
	else echo "[FAIL] plain binary did not finish parsing"; ok=0; fi; \
	if grep -q "Segmentation\|backtrace" .ps-plain.log 2>/dev/null; then \
	  echo "[FAIL] a backtrace in the plain server log"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.8 -- every USER/PASS config branch bounds its copy, live.
#
# THE DEFECT (D35's recorded siblings). Eleven branches parsed their token
# straight into char[64] fields of packed structs (camd35/cs378x clients,
# cccam F-lines, mgcamd users, telnet user/pass, http user/pass, freecccam
# user/password): a name over 63 chars spilled into the adjacent pass, a
# pass over 63 wrote straight over userhash -- or over the owning thread's
# pid/tid in the telnet/http config structs. TASK 3.5 fixed the newcamd
# branch by hand; 3.8 applies the same recipe to all the rest through
# cfgfield.h (one bounded copy, one house-format warning naming the config
# line and column, the truncated pair stays a working credential).
#
# THE SCENARIO. One server: HTTP USER/PASS and TELNET USER/PASS all 70
# chars. Post-fix the log carries exactly four "too long" warnings, the
# http Basic auth accepts the truncated 63-char pair (pre-fix: 401, the
# stored name was the full 70, bitten live), and the telnet prompt accepts
# the truncated pair and reaches the command prompt (pre-fix:
# TELNET-AUTH-FAIL -- the stored name was corrupted with the password's
# spill bytes). Pre-fix bites were taken on the 3.7 binary and are in
# REPORT-3.8-ar.md.
#
# PORTS: 16750 http, 16751 newcamd(unused), 16752 telnet. Distinct from ps
# (16740/41), sp (16730-33), cu (16720/21), ca (16710-13), dl (16700/01).
# ---------------------------------------------------------------------------

U_HPORT = 16750
U_NPORT = 16751
U_TPORT = 16752
U_CFG   = .u.cfg
UPROBE  = .u-probe.bin

.PHONY: u

$(UPROBE): u-probe.c
	$(CC) -O2 -m64 -std=gnu89 -Wall -Wextra -o $@ u-probe.c
	@chmod +x $@

u: $(UPROBE)
	@chmod +x $(UPROBE) 2>/dev/null || true   # make can skip the build rule after a restored workspace; do not depend on the bit surviving
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@command -v curl >/dev/null || { echo "curl required"; exit 1; }
	@pkill -9 -x multics 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(U_HPORT)\nHTTP USER: '; printf 'A%.0s' $$(seq 1 70); printf '\n'; \
	   printf 'HTTP PASS: '; printf 'P%.0s' $$(seq 1 70); printf '\n'; \
	   printf 'HTTP TITLE: mcs-u\nTELNET PORT: $(U_TPORT)\nTELNET USER: '; \
	   printf 'B%.0s' $$(seq 1 70); printf '\nTELNET PASS: '; \
	   printf 'Q%.0s' $$(seq 1 70); printf '\n\n'; \
	   printf '[ u38 ]\nCAID: 1884\nPORT: $(U_NPORT)\nNEWCAMD PORT: 0\n'; } > $(U_CFG); \
	ok=1; \
	rm -f .u-srv.log; \
	stdbuf -o0 -e0 $(BIN) -C $(U_CFG) -v > .u-srv.log 2>&1 & \
	srv=$$!; \
	code=000; \
	for i in $$(seq 1 40); do \
	  code=$$(curl -s -m 3 -u "$$(printf 'A%.0s' $$(seq 1 63)):$$(printf 'P%.0s' $$(seq 1 63))" -o /dev/null -w '%{http_code}' http://127.0.0.1:$(U_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] http accepts the truncated 63-char credential (pre-fix: 401)"; \
	else echo "[FAIL] http truncated login answered '$$code'"; ok=0; fi; \
	v=$$(./$(UPROBE) 127.0.0.1 $(U_TPORT) "$$(printf 'B%.0s' $$(seq 1 63))" "$$(printf 'Q%.0s' $$(seq 1 63))"); \
	if [ "$$v" = "TELNET-AUTH-OK" ]; then \
	  echo "  [ ok ] telnet accepts the truncated 63-char pair and reaches the prompt (pre-fix: FAIL)"; \
	else echo "[FAIL] telnet truncated login: $$v"; ok=0; fi; \
	nwarn=$$(grep -c "too long" .u-srv.log); \
	if [ "$$nwarn" = "4" ]; then \
	  echo "  [ ok ] exactly four house-format warnings: both fields, both surfaces"; \
	else echo "[FAIL] 'too long' warnings: $$nwarn, expected 4"; ok=0; fi; \
	if grep -q "Newcamd Server started on port $(U_NPORT)" .u-srv.log; then \
	  echo "  [ ok ] the config parsed to the end"; \
	else echo "[FAIL] config did not finish parsing"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then \
	  echo "  [ ok ] the server survived"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	[ "$$ok" = "1" ] || exit 1



# ---------------------------------------------------------------------------
# TASK 3.9 -- xmlescape() is bounded at the cell edge, live.
#
# THE DEFECT. Upstream's xmlescape() escaped into char exml[5000] on the
# web thread's stack with an unbounded dest walk, then strcpy()'d the
# result back into the caller's cell row -- both writes unchecked. Every
# call site passes char[N][2048] cells (grep-verified: 8/10/11/12 rows).
# Bitten pre-fix with the direct ASan probe (x64/probe_xmlescape: the
# production TU compiled with -Dxmlescape=real_xmlescape and a cell of
# '&' escaped for real): stack-buffer-overflow WRITE of size 5 at
# httpserver.c:1307 -- the &amp; entity memcpy. Post-fix the probe is
# clean and asserts the bounded truncation (escaped length<=2047, whole
# entities).
#
# THIS TARGET owns "the page stays valid": a live server whose profile
# name carries real '&'-escape characters (within every field bound).
# The /profiles and /servers pages pipe cells through xmlescape, so they
# must return 200 and show the ESCAPED name (p&amp;rofile-...), never the
# raw unescaped '&' markup; no truncation warning may appear (honest
# values); the config parses to the end and the server stays alive.
#
# PORT: 16760 http (registry: dl 16700/01, ca 16710-13, cu 16720/21,
# sp 16730-33, ps 16740/41, u 16750/51/52).
# ---------------------------------------------------------------------------

X_PORT = 16760
X_CFG  = .xe.cfg

.PHONY: xe

xe:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@make -s -C ../make-x64 x64/probe_xmlescape > /dev/null || { echo "[FAIL] probe build"; exit 1; }
	@chmod +x ../make-x64/x64/probe_xmlescape 2>/dev/null || true   # a restored workspace can lose the exec bit
	@../make-x64/x64/probe_xmlescape > .xe-probe.log 2>&1; \
	if [ $$? -eq 0 ]; then \
	  echo "  [ ok ] the ASan probe: production xmlescape stays inside its frame (pre-fix: stack-buffer-overflow WRITE)"; \
	else echo "[FAIL] ASan probe reports an overflow:"; cat .xe-probe.log | head -8; exit 1; fi
	@pkill -9 -x multics 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(X_PORT)\nHTTP TITLE: m&cs-xe\n\n[ xe&38 ]\nCAID: 1884\nPORT: 16761\nNEWCAMD PORT: 0\n'; } > $(X_CFG); \
	ok=1; \
	rm -f .xe-srv.log; \
	stdbuf -o0 -e0 $(BIN) -C $(X_CFG) -v > .xe-srv.log 2>&1 & \
	srv=$$!; \
	page=000; \
	for i in $$(seq 1 40); do \
	  page=$$(curl -s -m 3 -o .xe-profiles.html -w '%{http_code}' http://127.0.0.1:$(X_PORT)/profiles); \
	  [ "$$page" = "200" ] && break; sleep 1; \
	done; \
	if [ "$$page" = "200" ]; then \
	  echo "  [ ok ] /profiles renders (200)"; \
	else echo "[FAIL] /profiles answered '$$page'"; ok=0; fi; \
	if grep -q "xe&amp;38" .xe-profiles.html; then \
	  echo "  [ ok ] the profile name arrives escaped (xe&amp;38)"; \
	else echo "[FAIL] the escaped profile name is missing from the page"; ok=0; fi; \
	if grep -q "xe&38" .xe-profiles.html; then \
	  echo "[FAIL] the raw unescaped name leaked into the page"; ok=0; \
	else echo "  [ ok ] no raw unescaped name in the page"; fi; \
	code=$$(curl -s -m 3 -o .xe-servers.html -w '%{http_code}' http://127.0.0.1:$(X_PORT)/servers); \
	if [ "$$code" = "200" ]; then \
	  echo "  [ ok ] /servers renders (200)"; \
	else echo "[FAIL] /servers answered '$$code'"; ok=0; fi; \
	nwarn=$$(grep -c "cell text too long" .xe-srv.log); \
	if [ "$$nwarn" = "0" ]; then \
	  echo "  [ ok ] no truncation warning for honest values"; \
	else echo "[FAIL] truncation warning fired on honest values ($$nwarn)"; ok=0; fi; \
	if grep -q "Profile .* started\|Newcamd Server started on port 16761" .xe-srv.log; then \
	  echo "  [ ok ] the config parsed to the end"; \
	else echo "[FAIL] config did not finish parsing"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then \
	  echo "  [ ok ] the server survived"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.10 -- the DCW STATS config option, live.
#
# THE OPTION. acceptDCW()'s rejection counters (dcw.c, TASK phase 4) have
# been reachable only programmatically -- dcwstats_on was a test-side
# switch. 3.10 gives the operator the line the dcwstats.h header always
# promised: `DCW STATS: ON`. It is a sub-word of the EXISTING top-level
# DCW handler (the one that already owns DCW TIMEOUT / MAXFAILED / RETRY
# inside a profile). A second DCW branch earlier in that chain swallows
# those profile lines -- bitten once: sp's DCW TIMEOUT: 1500 never
# applied and the first ECM no longer failed as designed. Parsed with
# parse_boolean() as everywhere else: a typo reads OFF, visibly.
#
# THIS TARGET. Four boots, one reload:
#   stats build + ON            -> "config(l,c): DCW STATS: ON", parses to end
#   file rewrite (inotify)  -> the reset above runs, then the line
#                                  re-arms: a second "DCW STATS: ON"
#   stats build without it      -> default stays silent (counters off)
#   stats build + BOGUS value   -> "DCW STATS: OFF" (typo lands safe side)
#   stock build + ON            -> "DCW STATS ignored - this build has no
#                                  dcwstats counters" (configs stay portable)
# The counters' gather semantics are proven by build/test_dcwstats; the
# readout arrives with 3.11 (telnet) and 3.12 (web).
#
# PORT: 16770 http (registry: ... u 16750/51/52, xe 16760/61). The stats
# release is rebuilt every run: a binary that merely exists can be stale
# (bitten once -- ds then tested the previous task and reported a swallowed
# DCW TIMEOUT that the current sources no longer swallow). release-stats
# wipes x64/, so the dev binary is re-linked afterwards.
# The profile page must still show DCW TIMEOUT 1500ms: proof the STATS
# sub-word did not swallow the profile line.
# ---------------------------------------------------------------------------

DCS_PORT = 16770
DCS_CFG  = .dcs.cfg
# Not DS_CFG: that name belongs to the dstruct target, and a later
# assignment would silently retarget it (make variables are global).
STATS_BIN = ../dist/multics-r82a-stats-x64

.PHONY: ds

ds:
	@make -s -C ../make-x64 release-stats > /dev/null || { echo "[FAIL] stats release build"; exit 1; }
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@ok=1; \
	{ printf 'HTTP PORT: $(DCS_PORT)\nDCW STATS: ON\n\n[ d38 ]\nCAID: 1884\nPORT: 16771\nNEWCAMD PORT: 0\nDCW TIMEOUT: 1500\n'; } > $(DCS_CFG); \
	rm -f .ds1.log; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(DCS_CFG) -v > .ds1.log 2>&1 & srv=$$!; \
	sleep 4; \
	if grep -q "DCW STATS: ON" .ds1.log; then \
	  echo "  [ ok ] stats build: the line arms the counters (DCW STATS: ON)"; \
	else echo "[FAIL] stats build did not log DCW STATS: ON"; ok=0; fi; \
	if grep -q "Newcamd Server started on port 16771" .ds1.log; then \
	  echo "  [ ok ] the config parsed to the end"; \
	else echo "[FAIL] config did not finish parsing"; ok=0; fi; \
	curl -s -m 3 -o .ds-profile.html http://127.0.0.1:$(DCS_PORT)/profile?id=100; \
	if grep -q "DCW TIMEOUT</td><td>1500ms" .ds-profile.html; then \
	  echo "  [ ok ] profile DCW TIMEOUT: 1500 still applied (the STATS sub-word did not swallow it)"; \
	else echo "[FAIL] profile DCW TIMEOUT was swallowed (page has: $$(grep -o 'DCW TIMEOUT</td><td>[0-9]*ms' .ds-profile.html))"; ok=0; fi; \
	{ printf 'HTTP PORT: $(DCS_PORT)\nDCW STATS: ON\n\n[ d38 ]\nCAID: 1884\nPORT: 16771\nNEWCAMD PORT: 0\nDCW TIMEOUT: 1500\n'; } > $(DCS_CFG); \
	sleep 3; \
	n=$$(grep -c "DCW STATS: ON" .ds1.log); \
	chg=$$(grep -c "Config file Changed" .ds1.log); \
	if [ "$$n" = "2" ] && [ "$$chg" -ge "1" ]; then \
	  echo "  [ ok ] file reload: the reset runs, the line re-arms (2x + Config file Changed)"; \
	else echo "[FAIL] reload: DCW STATS: ON x$$n (want 2), Changed x$$chg"; ok=0; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; sleep 1; \
	{ printf 'HTTP PORT: $(DCS_PORT)\n\n[ d38 ]\nCAID: 1884\nPORT: 16771\nNEWCAMD PORT: 0\n'; } > $(DCS_CFG); \
	rm -f .ds2.log; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(DCS_CFG) -v > .ds2.log 2>&1 & srv=$$!; \
	sleep 4; \
	if grep -q "DCW STATS" .ds2.log; then \
	  echo "[FAIL] the default is not silent"; ok=0; \
	else echo "  [ ok ] no line, no counters: the default stays silent"; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; sleep 1; \
	{ printf 'HTTP PORT: $(DCS_PORT)\nDCW STATS: BOGUS\n\n[ d38 ]\nCAID: 1884\nPORT: 16771\nNEWCAMD PORT: 0\n'; } > $(DCS_CFG); \
	rm -f .ds3.log; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(DCS_CFG) -v > .ds3.log 2>&1 & srv=$$!; \
	sleep 4; \
	if grep -q "DCW STATS: OFF" .ds3.log; then \
	  echo "  [ ok ] a typo reads OFF, visibly (BOGUS -> DCW STATS: OFF)"; \
	else echo "[FAIL] a typo did not land OFF"; ok=0; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(DCS_CFG) -v > .ds4.log 2>&1 & srv=$$!; \
	sleep 4; \
	if strings $(BIN) 2>/dev/null | grep -q "this build has no dcwstats counters"; then \
	  if grep -q "DCW STATS ignored - this build has no dcwstats counters" .ds4.log; then \
	    echo "  [ ok ] stock build: the line parses but is inert, one warning says so"; \
	  else echo "[FAIL] stock build did not warn about the inert option"; ok=0; fi; \
	else echo "  [ -- ] flavor check: a stats build has no inert warning, check does not apply"; \
	fi; \
	if grep -q "Newcamd Server started on port 16771" .ds4.log; then \
	  echo "  [ ok ] the stock build parsed the same config to the end"; \
	else echo "[FAIL] stock build did not finish parsing"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	pkill -9 '^multics' 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.11 -- telnet `dcwstats` and `dcwstats reset`.
#
# THE COMMANDS. The counters and dcwstats_reset() have existed since the
# Phase-4 instrumentation. 3.10 armed the switch; this is the readout.
# `dcwstats` prints `dcwstats: ON|OFF` and one `<reason> <count>` line per
# slot, using dcwstats_reason_name() so the web page (3.12) can match the
# names. `dcwstats reset` calls dcwstats_reset() and says so. Any other
# second word is a usage line, not a silent no-op and not a reset. A stock
# build has no symbol and answers once: "this build has no counters; use
# the stats release". help lists the command on both builds.
#
# THE PROOF that reset actually zeros, not just prints. A cache peer pushes
# an all-zero CW (checksum holds, isnullDCW rejects, so the null/half-null
# counter is the one that moves). The peer is killed before the proof
# session so a later push cannot refill the counter between reset and the
# follow-up read. A bad sub-word in between must leave the count unchanged.
#
# PORTS: 16780 http, 16781 newcamd, 16782 telnet, 16783 cache, 16784 peer.
# Registry: ... ds 16770/71, xe 16760/61, u 16750/51/52.
# The stats release is rebuilt every run (a present binary can be stale).
# ---------------------------------------------------------------------------

DT_HPORT = 16780
DT_NPORT = 16781
DT_TPORT = 16782
DT_CACHE = 16783
DT_PEER  = 16784
DT_CFG   = .dt.cfg
DT_PROBE = .dt-probe.bin
DT_NULL  = 00000000000000000000000000000000

.PHONY: dt

$(DT_PROBE): dt-probe.c
	$(CC) -O2 -m64 -std=gnu89 -Wall -Wextra -o $@ dt-probe.c
	@chmod +x $@

dt: $(DT_PROBE) $(CACHEPEER)
	@make -s -C ../make-x64 release-stats > /dev/null || { echo "[FAIL] stats release build"; exit 1; }
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@ok=1; \
	{ printf 'HTTP PORT: $(DT_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(DT_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nCACHE PORT: $(DT_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(DT_PEER) { csp=1 }\nCACHE FILTER: OFF\n\n'; \
	  printf '[ dt ]\nCAID: 1884\nPORT: $(DT_NPORT)\nNEWCAMD PORT: 0\n'; } > $(DT_CFG); \
	rm -f .dt-srv.log .dt-peer.log .dt-pre.txt .dt-proof.txt; \
	CP_PUSH_HASH=a1669c0c CP_PUSH_SID=0064 CP_PUSH_CAID=1884 \
	  CP_REPUSH_MS=300 CP_REPUSH_SAME=1 \
	  stdbuf -o0 -e0 ./$(CACHEPEER) $(DT_CACHE) $(DT_PEER) $(DT_NULL) 1 > .dt-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(DT_CFG) -v > .dt-srv.log 2>&1 & srv=$$!; \
	got=0; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
	  ./$(DT_PROBE) 127.0.0.1 $(DT_TPORT) admin admin dcwstats > .dt-pre.txt 2>&1 || true; \
	  if grep -q "null/half-null [1-9]" .dt-pre.txt; then got=1; break; fi; \
	  sleep 1; \
	done; \
	if [ "$$got" = "1" ]; then \
	  echo "  [ ok ] a rejected all-zero CW moved the null/half-null counter"; \
	else echo "[FAIL] null/half-null never moved"; echo "--- peer ---"; tail -5 .dt-peer.log; echo "--- srv ---"; tail -8 .dt-srv.log; ok=0; fi; \
	kill $$peer 2>/dev/null; sleep 1; kill -9 $$peer 2>/dev/null; sleep 1; \
	./$(DT_PROBE) 127.0.0.1 $(DT_TPORT) admin admin dcwstats "dcwstats nope" dcwstats "dcwstats reset" dcwstats help > .dt-proof.txt 2>&1; \
	pr=$$?; \
	if [ "$$pr" != "0" ]; then echo "[FAIL] telnet proof session rc=$$pr"; cat .dt-proof.txt | head -8; ok=0; fi; \
	awk ' \
	  /^--- dcwstats ---$$/ { sec++; next } \
	  /^--- dcwstats nope ---$$/ { inn=1; next } \
	  /^--- help ---$$/ { inh=1; next } \
	  inn && /usage: dcwstats/ { usage=1; inn=0 } \
	  inh && /dcwstats/ { help=1 } \
	  sec==1 && /dcwstats: ON/ { on="ON" } \
	  sec==1 && /null\/half-null/ { pre=$$NF } \
	  sec==2 && /null\/half-null/ { mid=$$NF } \
	  sec==3 && /accepted / { acc=$$NF } \
	  sec==3 && /checksum / { sum=$$NF } \
	  sec==3 && /null\/half-null/ { post=$$NF } \
	  sec==3 && /repeat-3-bytes/ { rep=$$NF } \
	  sec==3 && /bad-dcw-list/ { bad=$$NF } \
	  END { \
	    if (on!="ON") { print "[FAIL] readout did not say ON (" on ")"; exit 1 } \
	    if (pre+0<1) { print "[FAIL] proof session lost the rejection count (" pre ")"; exit 1 } \
	    if (usage!=1) { print "[FAIL] a bad sub-word did not get a usage line"; exit 1 } \
	    if (mid+0<1) { print "[FAIL] a bad sub-word reset the counter (" mid ")"; exit 1 } \
	    if (post+0!=0 || acc+0!=0 || sum+0!=0 || rep+0!=0 || bad+0!=0) { \
	      print "[FAIL] reset did not zero (post=" post " acc=" acc ")"; exit 1 } \
	    if (help!=1) { print "[FAIL] help does not list dcwstats"; exit 1 } \
	    print "  [ ok ] dcwstats: ON, null/half-null " pre " (a bad sub-word left it)"; \
	    print "  [ ok ] dcwstats reset zeroed every counter"; \
	    print "  [ ok ] help lists dcwstats"; \
	  }' .dt-proof.txt || ok=0; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; sleep 1; \
	{ printf 'HTTP PORT: $(DT_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(DT_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf '\n[ dt ]\nCAID: 1884\nPORT: $(DT_NPORT)\nNEWCAMD PORT: 0\n'; } > $(DT_CFG); \
	rm -f .dt-off.log .dt-off.txt; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(DT_CFG) -v > .dt-off.log 2>&1 & srv=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "Telnet server started" .dt-off.log && break; sleep 1; done; \
	./$(DT_PROBE) 127.0.0.1 $(DT_TPORT) admin admin dcwstats > .dt-off.txt 2>&1; \
	if grep -q "dcwstats: OFF" .dt-off.txt && grep -q "null/half-null 0" .dt-off.txt; then \
	  echo "  [ ok ] no line, gathering OFF, counters zero"; \
	else echo "[FAIL] OFF readout"; cat .dt-off.txt; ok=0; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; sleep 1; \
	{ printf 'HTTP PORT: $(DT_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(DT_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\n\n[ dt ]\nCAID: 1884\nPORT: $(DT_NPORT)\nNEWCAMD PORT: 0\n'; } > $(DT_CFG); \
	rm -f .dt-stock.log .dt-stock.txt; \
	stdbuf -o0 -e0 $(BIN) -C $(DT_CFG) -v > .dt-stock.log 2>&1 & srv=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "Telnet server started" .dt-stock.log && break; sleep 1; done; \
	./$(DT_PROBE) 127.0.0.1 $(DT_TPORT) admin admin dcwstats help > .dt-stock.txt 2>&1; \
	if strings $(BIN) 2>/dev/null | grep -q "this build has no dcwstats counters"; then \
	  if grep -q "this build has no counters; use the stats release" .dt-stock.txt; then \
	    echo "  [ ok ] stock build answers once and names the release to use"; \
	  else echo "[FAIL] stock answer"; cat .dt-stock.txt; ok=0; fi; \
	  if grep -q "dcwstats" .dt-stock.txt && grep -q "Commands:" .dt-stock.txt; then \
	    echo "  [ ok ] stock help still lists the command"; \
	  else echo "[FAIL] stock help"; ok=0; fi; \
	else echo "  [ -- ] flavor check: a stats build answers with the table, stock-only telnet checks do not apply"; \
	fi; \
	if grep -q "Newcamd Server started on port $(DT_NPORT)" .dt-stock.log; then \
	  echo "  [ ok ] the stock build parsed the same config to the end"; \
	else echo "[FAIL] stock build did not finish parsing"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	pkill -9 '^multics' 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.12 -- DCW stats on the home page.
#
# THE SECTION. The home page (/) already shows uptime and totals, and its
# own refresh (/?action=div) replaces mainDiv. The counters live in that
# block, so a refresh moves the numbers. Names are dcwstats_reason_name(),
# the same list telnet prints, each passed through html_esc (3.2) before
# interpolation. Counts are %lu. A stock build does not reference the
# symbol and says so, once, like telnet.
#
# THE PROOF the numbers match. A cache peer pushes an all-zero CW so
# null/half-null moves. The peer is killed, then the page and a telnet
# `dcwstats` are read and every slot is compared. The div fragment must
# carry the same count and must not be a second document. A boot with no
# config line reads OFF and zeros on both surfaces. A stock boot renders
# the page (200, </html>, the old Uptime line still there) and the polite
# sentence, with no counter table.
#
# PORTS: 16790 http, 16791 newcamd, 16792 telnet, 16793 cache, 16794 peer.
# Registry: dt 16780-84, ds 16770/71, xe 16760/61, u 16750/51/52.
# ---------------------------------------------------------------------------

DW_HPORT = 16790
DW_NPORT = 16791
DW_TPORT = 16792
DW_CACHE = 16793
DW_PEER  = 16794
DW_CFG   = .dw.cfg
DW_NULL  = 00000000000000000000000000000000

.PHONY: dw

dw: $(DT_PROBE) $(CACHEPEER)
	@make -s -C ../make-x64 release-stats > /dev/null || { echo "[FAIL] stats release build"; exit 1; }
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@ok=1; \
	{ printf 'HTTP PORT: $(DW_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(DW_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nCACHE PORT: $(DW_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(DW_PEER) { csp=1 }\nCACHE FILTER: OFF\n\n'; \
	  printf '[ dw ]\nCAID: 1884\nPORT: $(DW_NPORT)\nNEWCAMD PORT: 0\n'; } > $(DW_CFG); \
	rm -f .dw-srv.log .dw-peer.log .dw-page.html .dw-div.html .dw-tel.txt; \
	CP_PUSH_HASH=a1669c0c CP_PUSH_SID=0064 CP_PUSH_CAID=1884 \
	  CP_REPUSH_MS=300 CP_REPUSH_SAME=1 \
	  stdbuf -o0 -e0 ./$(CACHEPEER) $(DW_CACHE) $(DW_PEER) $(DW_NULL) 1 > .dw-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(DW_CFG) -v > .dw-srv.log 2>&1 & srv=$$!; \
	got=0; code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
	  code=$$(curl -s -m 3 -u admin:admin -o .dw-page.html -w '%{http_code}' http://127.0.0.1:$(DW_HPORT)/); \
	  if [ "$$code" = "200" ] && grep -q "null/half-null</td><td>[1-9]" .dw-page.html; then got=1; break; fi; \
	  sleep 1; \
	done; \
	if [ "$$got" = "1" ]; then \
	  echo "  [ ok ] home page 200 and the rejected CW is on it"; \
	else echo "[FAIL] page never showed the rejection (http $$code)"; tail -6 .dw-srv.log; ok=0; fi; \
	kill $$peer 2>/dev/null; sleep 1; kill -9 $$peer 2>/dev/null; sleep 1; \
	code=$$(curl -s -m 3 -u admin:admin -o .dw-page.html -w '%{http_code}' http://127.0.0.1:$(DW_HPORT)/); \
	curl -s -m 3 --http0.9 -u admin:admin -o .dw-div.html 'http://127.0.0.1:$(DW_HPORT)/?action=div' || true; \
	./$(DT_PROBE) 127.0.0.1 $(DW_TPORT) admin admin dcwstats > .dw-tel.txt 2>&1; \
	if [ "$$code" != "200" ] || ! grep -q "</html>" .dw-page.html || ! grep -q "Uptime:" .dw-page.html || ! grep -q "</table>" .dw-page.html; then \
	  echo "[FAIL] the home page is not intact (http $$code)"; ok=0; \
	else echo "  [ ok ] the home page still renders (200, uptime, table, </html>)"; fi; \
	if grep -q "</table>" .dw-div.html && ! grep -q "<html" .dw-div.html; then \
	  echo "  [ ok ] the refresh fragment carries the table and is not a second document"; \
	else echo "[FAIL] action=div fragment"; ok=0; fi; \
	miss=0; \
	for name in accepted checksum null/half-null repeat-3-bytes bad-dcw-list; do \
	  web=$$(grep -o "$${name}</td><td>[0-9][0-9]*" .dw-page.html | head -1 | sed 's/.*<td>//'); \
	  div=$$(grep -o "$${name}</td><td>[0-9][0-9]*" .dw-div.html | head -1 | sed 's/.*<td>//'); \
	  tel=$$(awk -v n="$$name" '$$1==n { print $$2; exit }' .dw-tel.txt); \
	  if [ -z "$$web" ] || [ "$$web" != "$$tel" ] || [ "$$web" != "$$div" ]; then \
	    echo "[FAIL] $$name page='$$web' div='$$div' telnet='$$tel'"; miss=1; \
	  fi; \
	done; \
	if [ "$$miss" = "0" ]; then \
	  echo "  [ ok ] every counter on the page matches telnet and the refresh fragment"; \
	else ok=0; fi; \
	if grep -q "null/half-null</td><td>0" .dw-page.html; then \
	  echo "[FAIL] the matched count is zero -- the rejection was lost before the compare"; ok=0; \
	else echo "  [ ok ] the matched null/half-null count is not zero"; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; sleep 1; \
	{ printf 'HTTP PORT: $(DW_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(DW_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf '\n[ dw ]\nCAID: 1884\nPORT: $(DW_NPORT)\nNEWCAMD PORT: 0\n'; } > $(DW_CFG); \
	rm -f .dw-off.log .dw-off.html .dw-off-tel.txt; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(DW_CFG) -v > .dw-off.log 2>&1 & srv=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o .dw-off.html -w '%{http_code}' http://127.0.0.1:$(DW_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	./$(DT_PROBE) 127.0.0.1 $(DW_TPORT) admin admin dcwstats > .dw-off-tel.txt 2>&1 || true; \
	if grep -q "DCW stats</b> OFF" .dw-off.html && grep -q "null/half-null</td><td>0" .dw-off.html && grep -q "dcwstats: OFF" .dw-off-tel.txt; then \
	  echo "  [ ok ] no config line: page and telnet both say OFF and zero"; \
	else echo "[FAIL] OFF readout"; ok=0; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; sleep 1; \
	{ printf 'HTTP PORT: $(DW_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(DW_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\n\n[ dw ]\nCAID: 1884\nPORT: $(DW_NPORT)\nNEWCAMD PORT: 0\n'; } > $(DW_CFG); \
	rm -f .dw-stock.log .dw-stock.html; \
	stdbuf -o0 -e0 $(BIN) -C $(DW_CFG) -v > .dw-stock.log 2>&1 & srv=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o .dw-stock.html -w '%{http_code}' http://127.0.0.1:$(DW_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	if strings $(BIN) 2>/dev/null | grep -q "this build has no dcwstats counters"; then \
	  if [ "$$code" = "200" ] && grep -q "</html>" .dw-stock.html && grep -q "Uptime:" .dw-stock.html \
	     && grep -q "this build has no counters; use the stats release" .dw-stock.html \
	     && ! grep -q "<td>accepted</td>" .dw-stock.html; then \
	    echo "  [ ok ] stock page renders and names the release, with no counter table"; \
	  else echo "[FAIL] stock page (http $$code)"; ok=0; fi; \
	else echo "  [ -- ] flavor check: a stats build renders the counter table, stock-only page checks do not apply"; \
	fi; \
	if grep -q "Newcamd Server started on port $(DW_NPORT)" .dw-stock.log; then \
	  echo "  [ ok ] the stock build parsed the same config to the end"; \
	else echo "[FAIL] stock build did not finish parsing"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	pkill -9 '^multics' 2>/dev/null; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.13 — cardsids_update floor. The probe calls the production function:
# 100 failures show -100, 50 more stay there, one success returns to 0.
# The server half arms the consumer gate (DCW MAXFAILED: 100) and shows it
# on the profile page. Profile ids start at 0x64.
#
# PORTS: 16800 http, 16801 newcamd.
# Registry: dw 16790-94, dt 16780-84, ds 16770/71, xe 16760/61.
# ---------------------------------------------------------------------------

FL_HPORT = 16800
FL_NPORT = 16801
FL_CFG   = .fl.cfg
FL_PROBE = ../make-x64/x64/probe_cardsids

.PHONY: fl

fl:
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@make -s -C ../make-x64 x64/probe_cardsids > /dev/null || { echo "[FAIL] cardsids probe build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@test -x $(FL_PROBE) || { echo "[FAIL] probe missing"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@ok=1; \
	rm -f .fl-probe.log; \
	$(FL_PROBE) > .fl-probe.log 2>&1; prc=$$?; \
	cat .fl-probe.log; \
	if [ "$$prc" != "0" ]; then echo "[FAIL] probe exit $$prc"; ok=0; fi; \
	grep -q "counter after 100 failures: -100" .fl-probe.log \
	  && echo "  [ ok ] the counter is shown at -100" \
	  || { echo "[FAIL] the counter was not shown at the floor"; ok=0; }; \
	grep -q "counter after 50 more failures: -100" .fl-probe.log \
	  && echo "  [ ok ] 50 further failures did not push past -100" \
	  || { echo "[FAIL] the floor receded"; ok=0; }; \
	grep -q "counter after one success: 0" .fl-probe.log \
	  && echo "  [ ok ] one success returned the counter to 0" \
	  || { echo "[FAIL] a success did not recover the floor"; ok=0; }; \
	nfloor=$$(grep -c "at floor -100" .fl-probe.log); \
	nrec=$$(grep -c "recovered from the floor" .fl-probe.log); \
	if [ "$$nfloor" = "2" ] && [ "$$nrec" = "1" ]; then \
	  echo "  [ ok ] the floor line fired once per provider, the recovery once, and the extra failures stayed quiet"; \
	else echo "[FAIL] floor log count $$nfloor (want 2), recovery $$nrec (want 1)"; ok=0; fi; \
	grep -q "ceiling after a failure: 100" .fl-probe.log \
	  && echo "  [ ok ] the +100 ceiling is unchanged" \
	  || { echo "[FAIL] the +100 ceiling moved"; ok=0; }; \
	{ printf 'HTTP PORT: $(FL_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n\n'; \
	  printf '[ fl ]\nCAID: 1884\nPORT: $(FL_NPORT)\nNEWCAMD PORT: 0\n'; \
	  printf 'DCW MAXFAILED: 100\n'; } > $(FL_CFG); \
	rm -f .fl-srv.log .fl-page.html; \
	stdbuf -o0 -e0 $(BIN) -C $(FL_CFG) -v > .fl-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o .fl-page.html -w '%{http_code}' "http://127.0.0.1:$(FL_HPORT)/profile?id=100"); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	if [ "$$code" = "200" ] && grep -q "DCW MAXFAILED</td><td>100</td>" .fl-page.html; then \
	  echo "  [ ok ] the profile page shows DCW MAXFAILED 100, the gate that reads this counter"; \
	else echo "[FAIL] profile page did not show DCW MAXFAILED 100 (http $$code)"; ok=0; fi; \
	if grep -q "Newcamd Server started on port $(FL_NPORT)" .fl-srv.log; then \
	  echo "  [ ok ] the server parsed DCW MAXFAILED and finished starting"; \
	else echo "[FAIL] the server did not finish parsing"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] the server survived"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV" .fl-srv.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	kill $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	pkill -9 '^multics' 2>/dev/null; \
	rm -f $(FL_CFG) .fl-probe.log .fl-srv.log .fl-page.html; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.14 — BAD-DCW. The documented key is rejected and counted. A key
# that is not on the list is delivered. No line means the list is off
# and that key is delivered. A reload that drops the line clears the
# list. A stock build rejects too; it has no counter.
#
# The listed key is the one the public docs name. It passes checksum,
# so the reject is the list and not one of the three older tests.
#
# PORTS: 16810 http, 16811 newcamd, 16812 telnet, 16813 cache, 16814 peer.
# Registry: fl 16800/01, dw 16790-94.
# ---------------------------------------------------------------------------

BD_HPORT = 16810
BD_NPORT = 16811
BD_TPORT = 16812
BD_CACHE = 16813
BD_PEER  = 16814
BD_CFG   = .bd.cfg
BD_LISTED = FDFFFFFBFDFFFFFBFDFFFFFBFDFFFFFB
BD_OTHER  = 11223366445566FF77889998AABBCC31

.PHONY: bd

bd: $(CACHEPEER) $(NCCLIENT)
	@make -s -C ../make-x64 release-stats > /dev/null || { echo "[FAIL] stats release build"; exit 1; }
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@test -x $(STATS_BIN) || { echo "[FAIL] stats release missing"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@ok=1; \
	{ printf 'HTTP PORT: $(BD_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(BD_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nCACHE PORT: $(BD_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(BD_PEER) { csp=1 }\nCACHE FILTER: OFF\n'; \
	  printf 'BAD-DCW: FD FF FF FB FD FF FF FB FD FF FF FB FD FF FF FB\n\n'; \
	  printf '[ bd ]\nCAID: 1884\nPORT: $(BD_NPORT)\nUSER: u1 p1\n'; } > $(BD_CFG); \
	rm -f .bd-srv.log .bd-peer.log .bd-cli.log .bd-page.html; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(BD_CACHE) $(BD_PEER) $(BD_LISTED) 1 > .bd-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(BD_CFG) -v > .bd-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(BD_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "advertised card" .bd-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECM_GAP_MS=200 ./$(NCCLIENT) 127.0.0.1 $(BD_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .bd-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .bd-cli.log); \
	if [ "$$n" = "0" ]; then echo "  [ ok ] the listed key was not delivered"; \
	else echo "[FAIL] the listed key was delivered $$n times"; ok=0; fi; \
	curl -s -m 3 -u admin:admin -o .bd-page.html http://127.0.0.1:$(BD_HPORT)/; \
	hits=$$(grep -o 'bad-dcw-list</td><td>[0-9]*' .bd-page.html | grep -o '[0-9]*$$'); \
	if [ -n "$$hits" ] && [ "$$hits" != "0" ]; then \
	  echo "  [ ok ] bad-dcw-list counted $$hits"; \
	else echo "[FAIL] bad-dcw-list did not move (got '$$hits')"; ok=0; fi; \
	if grep -q "BAD-DCW: 1 entry enforced" .bd-srv.log; then \
	  echo "  [ ok ] the server enforced the one listed key"; \
	else echo "[FAIL] the list was not enforced"; ok=0; fi; \
	kill $$peer $$srv 2>/dev/null; sleep 1; kill -9 $$peer $$srv 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; sleep 1; \
	rm -f .bd-peer.log .bd-cli.log .bd-srv.log .bd-page.html; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(BD_CACHE) $(BD_PEER) $(BD_OTHER) 1 > .bd-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(BD_CFG) -v > .bd-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(BD_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10; do grep -q "advertised card" .bd-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECM_GAP_MS=200 ./$(NCCLIENT) 127.0.0.1 $(BD_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .bd-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .bd-cli.log); \
	curl -s -m 3 -u admin:admin -o .bd-page.html http://127.0.0.1:$(BD_HPORT)/; \
	hits2=$$(grep -o 'bad-dcw-list</td><td>[0-9]*' .bd-page.html | grep -o '[0-9]*$$'); \
	if [ "$$n" = "2" ] && grep -q "dcw=$(BD_OTHER)" .bd-cli.log && [ "$$hits2" = "0" ]; then \
	  echo "  [ ok ] a key that is not on the list was delivered, and bad-dcw-list stayed 0"; \
	else echo "[FAIL] unlisted key (delivered $$n, bad-dcw-list '$$hits2')"; ok=0; fi; \
	{ printf 'HTTP PORT: $(BD_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(BD_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nCACHE PORT: $(BD_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(BD_PEER) { csp=1 }\nCACHE FILTER: OFF\n\n'; \
	  printf '[ bd ]\nCAID: 1884\nPORT: $(BD_NPORT)\nUSER: u1 p1\n'; } > $(BD_CFG); \
	cleared=0; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  grep -q "BAD-DCW: list cleared" .bd-srv.log && cleared=1 && break; \
	  sleep 1; \
	done; \
	if [ "$$cleared" = "1" ]; then \
	  echo "  [ ok ] removing the line reloads and clears the list"; \
	else echo "[FAIL] the list was not cleared on reload"; ok=0; fi; \
	kill $$peer $$srv 2>/dev/null; sleep 1; kill -9 $$peer $$srv 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; sleep 1; \
	rm -f .bd-srv.log .bd-peer.log .bd-cli.log .bd-page.html; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(BD_CACHE) $(BD_PEER) $(BD_LISTED) 1 > .bd-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(BD_CFG) -v > .bd-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(BD_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "advertised card" .bd-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECM_GAP_MS=200 ./$(NCCLIENT) 127.0.0.1 $(BD_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .bd-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .bd-cli.log); \
	if [ "$$n" = "2" ] && grep -q "dcw=$(BD_LISTED)" .bd-cli.log && ! grep -q "BAD-DCW:" .bd-srv.log; then \
	  echo "  [ ok ] no BAD-DCW line: that key is delivered, and the list stays off"; \
	else echo "[FAIL] off-by-default (delivered $$n)"; ok=0; fi; \
	kill $$peer $$srv 2>/dev/null; sleep 1; kill -9 $$peer $$srv 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	pkill -9 '^multics' 2>/dev/null; sleep 1; \
	{ printf 'HTTP PORT: $(BD_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'CACHE PORT: $(BD_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(BD_PEER) { csp=1 }\nCACHE FILTER: OFF\n'; \
	  printf 'BAD-DCW: FD FF FF FB FD FF FF FB FD FF FF FB FD FF FF FB\n\n'; \
	  printf '[ bd ]\nCAID: 1884\nPORT: $(BD_NPORT)\nUSER: u1 p1\n'; } > $(BD_CFG); \
	rm -f .bd-stock.log .bd-peer.log .bd-cli.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(BD_CACHE) $(BD_PEER) $(BD_LISTED) 1 > .bd-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(BD_CFG) -v > .bd-stock.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(BD_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10; do grep -q "advertised card" .bd-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECM_GAP_MS=200 NC_FILL=3 ./$(NCCLIENT) 127.0.0.1 $(BD_NPORT) u1 p1 0102030405060708091011121314 0 1884 0064 > .bd-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .bd-cli.log); \
	if [ "$$n" = "0" ] && grep -q "BAD-DCW: 1 entry enforced" .bd-stock.log; then \
	  echo "  [ ok ] the stock build rejects the listed key too"; \
	else echo "[FAIL] stock reject (delivered $$n, http $$code)"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] the server survived"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV" .bd-srv.log .bd-stock.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; \
	rm -f $(BD_CFG) .bd-srv.log .bd-stock.log .bd-peer.log .bd-cli.log .bd-page.html; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.15 — a profile may turn one DCWFILTER gate off. The global gate
# stays on. Profile pfb says CHECKSUM OFF and receives a key that fails
# the checksum. Profile pfa says nothing and does not receive that key,
# then does receive a key that passes. A null key is not part of this
# test: no profile is allowed to rescue one.
#
# PORTS: 16820 http, 16821 pfa, 16822 pfb, 16823 telnet, 16824 cache,
# 16825 peer. Registry: bd 16810-14.
# ---------------------------------------------------------------------------

PF_HPORT = 16820
PF_APORT = 16821
PF_BPORT = 16822
PF_TPORT = 16823
PF_CACHE = 16824
PF_PEER  = 16825
PF_CFG   = .pf.cfg
PF_BAD   = 11223366445566FF77889998AABBCC32
PF_GOOD  = 11223366445566FF77889998AABBCC31

.PHONY: pf

pf: $(CACHEPEER) $(NCCLIENT)
	@make -s -C ../make-x64 release-stats > /dev/null || { echo "[FAIL] stats release build"; exit 1; }
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@test -x $(STATS_BIN) || { echo "[FAIL] stats release missing"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(PF_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(PF_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nCACHE PORT: $(PF_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(PF_PEER) { csp=1 }\nCACHE FILTER: OFF\n\n'; \
	  printf '[ pfa ]\nCAID: 1884\nPORT: $(PF_APORT)\nUSER: ua pa\n\n'; \
	  printf '[ pfb ]\nCAID: 1884\nPORT: $(PF_BPORT)\nUSER: ub pb\n'; \
	  printf 'DCWFILTER CHECKSUM: OFF\n'; } > $(PF_CFG); \
	ok=1; \
	rm -f .pf-srv.log .pf-peer.log .pf-cli.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(PF_CACHE) $(PF_PEER) $(PF_BAD) 1 > .pf-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(PF_CFG) -v > .pf-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(PF_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "advertised card" .pf-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	if grep -q "DCWFILTER CHECKSUM: OFF (profile pfb" .pf-srv.log; then \
	  echo "  [ ok ] the profile override was parsed"; \
	else echo "[FAIL] the profile override was not parsed"; ok=0; fi; \
	if ! grep -q "advertised card" .pf-peer.log; then echo "[FAIL] the peer never advertised"; ok=0; fi; \
	NC_ECM_GAP_MS=200 ./$(NCCLIENT) 127.0.0.1 $(PF_BPORT) ub pb 0102030405060708091011121314 0 1884 0064 > .pf-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .pf-cli.log); \
	if [ "$$n" = "2" ] && grep -q "dcw=$(PF_BAD)" .pf-cli.log; then \
	  echo "  [ ok ] the profile with checksum off received the key"; \
	else echo "[FAIL] lenient profile (delivered $$n)"; ok=0; fi; \
	rm -f .pf-cli.log; \
	NC_ECM_GAP_MS=200 ./$(NCCLIENT) 127.0.0.1 $(PF_APORT) ua pa 0102030405060708091011121314 0 1884 0064 > .pf-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .pf-cli.log); \
	if [ "$$n" = "0" ]; then \
	  echo "  [ ok ] the profile that inherits the global gate did not receive it"; \
	else echo "[FAIL] strict profile received the bad key ($$n)"; ok=0; fi; \
	kill $$peer $$srv 2>/dev/null; sleep 1; kill -9 $$peer $$srv 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; sleep 1; \
	rm -f .pf-good.log .pf-peer.log .pf-cli.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(PF_CACHE) $(PF_PEER) $(PF_GOOD) 1 > .pf-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(STATS_BIN) -C $(PF_CFG) -v > .pf-good.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(PF_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "advertised card" .pf-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	if ! grep -q "advertised card" .pf-peer.log; then echo "[FAIL] the good-key peer never advertised"; ok=0; fi; \
	NC_ECM_GAP_MS=200 ./$(NCCLIENT) 127.0.0.1 $(PF_APORT) ua pa 0102030405060708091011121314 0 1884 0065 > .pf-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .pf-cli.log); \
	if [ "$$n" = "2" ] && grep -q "dcw=$(PF_GOOD)" .pf-cli.log; then \
	  echo "  [ ok ] that same profile still receives a key that passes"; \
	else echo "[FAIL] strict profile good key (delivered $$n, http $$code)"; ok=0; fi; \
	kill $$peer $$srv 2>/dev/null; sleep 1; kill -9 $$peer $$srv 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; sleep 1; \
	rm -f .pf-stock.log .pf-peer.log .pf-cli.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(PF_CACHE) $(PF_PEER) $(PF_BAD) 1 > .pf-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(PF_CFG) -v > .pf-stock.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(PF_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "advertised card" .pf-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECM_GAP_MS=200 ./$(NCCLIENT) 127.0.0.1 $(PF_BPORT) ub pb 0102030405060708091011121314 0 1884 0066 > .pf-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .pf-cli.log); \
	if [ "$$n" = "2" ] && grep -q "dcw=$(PF_BAD)" .pf-cli.log && grep -q "DCWFILTER CHECKSUM: OFF (profile pfb" .pf-stock.log; then \
	  echo "  [ ok ] the stock build honors the profile override too"; \
	else echo "[FAIL] stock override (delivered $$n, http $$code)"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] the server survived"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	if grep -qi "segmentation\\|SIGSEGV" .pf-srv.log .pf-good.log .pf-stock.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(PF_CFG) .pf-srv.log .pf-good.log .pf-stock.log .pf-peer.log .pf-cli.log; \
	else echo "[FAIL] logs kept under tests/.pf-*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.16 — a cache push already in flight must not revive an ECM whose
# failure was committed. The peer delays its reply until the ECM is close
# to DCW TIMEOUT. MCS_316_HOLD_MS then holds lockcache across that timeout,
# so the push is piped only after FAILED. The client stays on the socket
# (NC_LINGER_MS) and must not receive a key. A separate run, with no hold
# and an immediate reply, must still deliver.
#
# PORTS: 16840 http, 16841 newcamd, 16842 cache, 16843 peer, 16844 telnet.
# Registry: pf 16820-25, bd 16810-14.
# ---------------------------------------------------------------------------

FS_HPORT = 16840
FS_NPORT = 16841
FS_CACHE = 16842
FS_PEER  = 16843
FS_TPORT = 16844
FS_CFG   = .fs.cfg
FS_KEY   = 0102030405060708091011121314
FS_CW    = 11223366445566FF77889998AABBCC31

.PHONY: fs

fs: $(CACHEPEER) $(NCCLIENT)
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(FS_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(FS_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'CACHE PORT: $(FS_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(FS_PEER) { csp=1 }\nCACHE FILTER: OFF\n\n'; \
	  printf '[fs]\nCAID: 1884\nPORT: $(FS_NPORT)\nDCW TIMEOUT: 2000\n'; \
	  printf 'USER: ua pa\nUSER: ub pb\n'; } > $(FS_CFG); \
	ok=1; \
	rm -f .fs-srv.log .fs-peer.log .fs-cli.log; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(FS_CACHE) $(FS_PEER) $(FS_CW) 1 > .fs-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(FS_CFG) -v > .fs-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(FS_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "advertised card" .fs-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(FS_NPORT) ua pa $(FS_KEY) 0 1884 0064 > .fs-cli.log 2>&1; \
	n=$$(grep -c "DELIVERED A CONTROL WORD" .fs-cli.log); \
	if [ "$$n" = "1" ] && grep -q "dcw=$(FS_CW)" .fs-cli.log; then \
	  echo "  [ ok ] a push that arrives while the ECM is waiting is still delivered"; \
	else echo "[FAIL] live waiter (delivered $$n)"; ok=0; fi; \
	kill $$peer $$srv 2>/dev/null; sleep 1; kill -9 $$peer $$srv 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; sleep 1; \
	rm -f .fs-srv.log .fs-peer.log .fs-cli.log; \
	{ printf 'HTTP PORT: $(FS_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(FS_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'CACHE PORT: $(FS_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(FS_PEER) { csp=1 }\nCACHE FILTER: OFF\n\n'; \
	  printf '[fs]\nCAID: 1884\nPORT: $(FS_NPORT)\nDCW TIMEOUT: 600\n'; \
	  printf 'USER: ua pa\nUSER: ub pb\n'; } > $(FS_CFG); \
	CP_ANSWER_DELAY_MS=200 stdbuf -o0 -e0 ./$(CACHEPEER) $(FS_CACHE) $(FS_PEER) $(FS_CW) 1 > .fs-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	MCS_316_HOLD_MS=500 stdbuf -o0 -e0 $(BIN) -C $(FS_CFG) -v > .fs-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(FS_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "advertised card" .fs-peer.log && break; sleep 0.5; done; \
	sleep 1; \
	NC_ECMS=1 NC_LINGER_MS=1500 ./$(NCCLIENT) 127.0.0.1 $(FS_NPORT) ub pb $(FS_KEY) 0 1884 0065 > .fs-cli.log 2>&1; \
	if grep -q "decode-failed" .fs-cli.log; then \
	  echo "  [ ok ] the ECM failed before the delayed push was applied"; \
	else echo "[FAIL] the ECM did not fail first"; ok=0; fi; \
	if grep -q "LATE CONTROL WORD" .fs-cli.log; then \
	  echo "[FAIL] a late push revived the failed ECM"; ok=0; \
	elif grep -q "linger: no further reply" .fs-cli.log || grep -q "linger: no control word" .fs-cli.log; then \
	  echo "  [ ok ] the client received nothing after the failure"; \
	else echo "[FAIL] linger did not report"; ok=0; fi; \
	if grep -q "not reviving a failed ECM" .fs-srv.log; then \
	  echo "  [ ok ] the in-flight push was refused"; \
	else echo "[FAIL] the race did not reach the guard"; ok=0; fi; \
	if grep -q "=> cw to client 'ub'" .fs-srv.log; then \
	  echo "[FAIL] the server sent a key to the failed client"; ok=0; \
	else echo "  [ ok ] the server did not send a key after the failure"; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] the server survived"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	if grep -qi "segmentation\\|SIGSEGV" .fs-srv.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(FS_CFG) .fs-srv.log .fs-peer.log .fs-cli.log; \
	else echo "[FAIL] logs kept under tests/.fs-*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.17 — a peer advertises 1024 cards. The server must ask it for a
# listed card and not for an unlisted one, and the card page must show all
# 1024. The read-past-the-end proof is the ASan unit test; this target is
# the live path (peer_card_binarysearch).
#
# PORTS: 16850 http, 16851 hit newcamd, 16852 cache, 16853 peer,
#         16854 telnet, 16855 miss newcamd.
# Registry: fs 16840-44, pf 16820-25, bd 16810-14.
# ---------------------------------------------------------------------------

PC_HPORT = 16850
PC_NPORT = 16851
PC_CACHE = 16852
PC_PEER  = 16853
PC_TPORT = 16854
PC_MPORT = 16855
PC_CFG   = .pc.cfg
PC_KEY   = 0102030405060708091011121314
PC_CW    = 11223366445566FF77889998AABBCC31
CARDFILL = .cardfill.bin

.PHONY: pc

$(CARDFILL): cardfill.c
	$(CC) -O2 -m64 -std=gnu89 -Wall -Wextra -o $@ cardfill.c

pc: $(CARDFILL) $(NCCLIENT)
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(PC_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(PC_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'CACHE PORT: $(PC_CACHE)\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(PC_PEER) { csp=1 }\nCACHE FILTER: OFF\n\n'; \
	  printf '[hit]\nCAID: 1884\nPORT: $(PC_NPORT)\nDCW TIMEOUT: 2000\n'; \
	  printf 'USER: ua pa\n\n'; \
	  printf '[miss]\nCAID: 1801\nPORT: $(PC_MPORT)\nDCW TIMEOUT: 1500\n'; \
	  printf 'USER: um pm\n'; } > $(PC_CFG); \
	ok=1; \
	rm -f .pc-srv.log .pc-peer.log .pc-hit.log .pc-miss.log .pc-cache.html .pc-peer.html; \
	stdbuf -o0 -e0 ./$(CARDFILL) $(PC_CACHE) $(PC_PEER) $(PC_CW) > .pc-peer.log 2>&1 & peer=$$!; \
	sleep 1; \
	stdbuf -o0 -e0 $(BIN) -C $(PC_CFG) -v > .pc-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(PC_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	if [ "$$code" != "200" ]; then echo "[FAIL] server did not answer HTTP"; ok=0; fi; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do grep -q "advertised 1024 cards" .pc-peer.log && break; sleep 0.5; done; \
	if grep -q "advertised 1024 cards" .pc-peer.log; then \
	  echo "  [ ok ] the peer advertised 1024 cards"; \
	else echo "[FAIL] the card list was not sent"; ok=0; fi; \
	sleep 1; \
	curl -s -m 10 -u admin:admin -o .pc-cache.html "http://127.0.0.1:$(PC_HPORT)/cache?list=all"; \
	id=$$(sed -n 's/.*cachepeer?id=\([0-9][0-9]*\).*/\1/p' .pc-cache.html | head -1); \
	if [ -n "$$id" ]; then \
	  curl -s -m 15 -u admin:admin -o .pc-peer.html "http://127.0.0.1:$(PC_HPORT)/cachepeer?id=$$id"; \
	  nopt=$$(grep -o '<option>1884:' .pc-peer.html | wc -l | tr -d ' '); \
	  if [ "$$nopt" = "1024" ]; then \
	    echo "  [ ok ] the server stored 1024 cards"; \
	  else echo "[FAIL] card page shows $$nopt cards, want 1024"; ok=0; fi; \
	  if grep -q '<option>1884:000000' .pc-peer.html; then \
	    echo "  [ ok ] the listed card is on the page"; \
	  else echo "[FAIL] 1884:000000 missing from the card page"; ok=0; fi; \
	  if grep -q '<option>1801:' .pc-peer.html; then \
	    echo "[FAIL] the unlisted card is on the page"; ok=0; \
	  else echo "  [ ok ] the unlisted card is not on the page"; fi; \
	else echo "[FAIL] no cache peer on the cache page"; ok=0; fi; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PC_NPORT) ua pa $(PC_KEY) 0 1884 0064 > .pc-hit.log 2>&1; \
	if grep -q "DELIVERED A CONTROL WORD" .pc-hit.log && grep -q "dcw=$(PC_CW)" .pc-hit.log; then \
	  echo "  [ ok ] a listed card was requested and the key was delivered"; \
	else echo "[FAIL] listed card was not delivered"; ok=0; fi; \
	if grep -q "request caid 1884" .pc-peer.log; then \
	  echo "  [ ok ] the server asked the peer for the listed card"; \
	else echo "[FAIL] the server did not ask for the listed card"; ok=0; fi; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PC_MPORT) um pm $(PC_KEY) 0 1801 0065 > .pc-miss.log 2>&1; \
	if grep -q "decode-failed" .pc-miss.log; then \
	  echo "  [ ok ] an unlisted card was not decoded"; \
	else echo "[FAIL] the unlisted card did not fail"; ok=0; fi; \
	if grep -q "request caid 1801" .pc-peer.log; then \
	  echo "[FAIL] the server asked the peer for a card that is not on the list"; ok=0; \
	else echo "  [ ok ] the server did not ask for the unlisted card"; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "  [ ok ] the server survived a 1024-card list"; \
	else echo "[FAIL] the server died"; ok=0; fi; \
	if grep -qi "segmentation\\|SIGSEGV" .pc-srv.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	kill $$srv $$peer 2>/dev/null; \
	for i in 1 2 3 4 5; do kill -0 $$srv 2>/dev/null || break; kill -9 $$srv 2>/dev/null; sleep 1; done; \
	kill -9 $$peer 2>/dev/null; \
	pkill -9 '^multics' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(PC_CFG) .pc-srv.log .pc-peer.log .pc-hit.log .pc-miss.log .pc-cache.html .pc-peer.html; \
	else echo "[FAIL] logs kept under tests/.pc-*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK 3.18 — HTTP restart comes back without a thread stopper.
# The old process exits (that is what ends its threads) and a new process
# binds the same ports. D47. No join, no cancel.
#
# PORTS: 16860 http, 16861 newcamd, 16864 telnet.
# Registry: pc 16850-55, fs 16840-44.
# ---------------------------------------------------------------------------

RS_HPORT = 16860
RS_NPORT = 16861
RS_TPORT = 16864
RS_CFG   = .rs.cfg

.PHONY: rs

rs:
	@make -s -C ../make-x64 link > /dev/null || { echo "[FAIL] dev build"; exit 1; }
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(RS_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(RS_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n\n'; \
	  printf '[rs]\nCAID: 1884\nPORT: $(RS_NPORT)\nUSER: ua pa\n'; } > $(RS_CFG); \
	ok=1; \
	rm -f .rs-srv.log; \
	stdbuf -o0 -e0 $(BIN) -C $(RS_CFG) -v > .rs-srv.log 2>&1 & srv=$$!; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(RS_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	if [ "$$code" = "200" ]; then echo "  [ ok ] the server answered before restart"; \
	else echo "[FAIL] server did not answer HTTP"; ok=0; fi; \
	curl -s -m 8 -u admin:admin -o /dev/null -w '' http://127.0.0.1:$(RS_HPORT)/restart; \
	code=000; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
	  if kill -0 $$srv 2>/dev/null; then sleep 1; continue; fi; \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(RS_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; \
	done; \
	if [ "$$code" = "200" ]; then echo "  [ ok ] after restart the page answered again"; \
	else echo "[FAIL] the page did not come back ($$code)"; ok=0; fi; \
	if kill -0 $$srv 2>/dev/null; then echo "[FAIL] the old process is still running"; ok=0; \
	else echo "  [ ok ] the old process exited"; fi; \
	if grep -q "Restarting..." .rs-srv.log && grep -q "Stopped." .rs-srv.log; then \
	  echo "  [ ok ] restart logged Restarting and Stopped"; \
	else echo "[FAIL] restart did not log"; ok=0; fi; \
	nhttp=$$(grep -c "HTTP server started" .rs-srv.log); \
	if [ "$$nhttp" -ge 2 ]; then echo "  [ ok ] the new process bound the page port"; \
	else echo "[FAIL] HTTP server started $$nhttp time(s), want 2"; ok=0; fi; \
	if grep -q "bind port failed" .rs-srv.log; then echo "[FAIL] a port was still held"; ok=0; \
	else echo "  [ ok ] no port was left held"; fi; \
	if grep -qi "segmentation\\|SIGSEGV" .rs-srv.log; then echo "[FAIL] logged a segfault"; ok=0; fi; \
	pkill -9 '^multics' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(RS_CFG) .rs-srv.log; \
	else echo "[FAIL] logs kept under tests/.rs-*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# ce -- TASK R1: the three cache-exchange gates, end to end.
#
# A real cccam cacheex mode-3 peer (.cxpeer.bin) logs into the real server
# through the real srv-cccam.c handshake and pushes CC_MSG_CACHE_PUSH
# frames, exactly the bytes a r107/oscam partner sends. Three scenarios:
#
#   baseline : no gate armed -- a good push for a PENDING newcamd ECM is
#              delivered, all three gate counters stay zero;
#   gated    : DEFAULT CACHEEX LOCAL_ONLY/BLOCK_FAKE_CW/CWCHECK armed,
#              DCWFILTER forced OFF so the fake gate's OWN checksum run is
#              what catches the corrupted key -- unsolicited push dropped
#              (cacheex-local-only), fake push dropped (cacheex-fake-cw),
#              first good push held (cacheex-confirm-wait), second
#              identical push crosses the floor and the client is served;
#   parser   : MAXHOP_LG prints its notice, CWCHECK clamps to the cap.
#
# GOODCW is a key with all four checksumDCW() group sums correct; the
# `bad` job flag bumps cw[15] so checksumDCW() fails. Needs the stats
# release (the counters are MCS_DCWSTATS-gated).
# ---------------------------------------------------------------------------
CXPEER    = .cxpeer.bin
CEDCW     = cedcw.py
CE_BIN   ?= ../bin/multics-r82a-stats-x64
CE_HPORT  = 15700
CE_TPORT  = 15701
CE_NPORT  = 15702
CE_CPORT  = 15703
CE_CFG    = .ce-srv.cfg
CE_GOOD   = 01020306112233660A0B0C21515253F6
CE_KEY    = 0102030405060708091011121314

.PHONY: ce

ce: $(CXPEER)
	@test -x $(CE_BIN) || { echo "build the stats release first: make -C ../make-x64 release-stats"; exit 1; }
	@chmod +x $(CXPEER) $(NCCLIENT) 2>/dev/null || true
	@rm -f .ce-s1.log .ce-s2.log .ce-s3.log .ce-nc1.out .ce-nc2.out .ce-peer.out .ce-stats.out $(CE_CFG); ok=1; \
	start_srv() { stdbuf -o0 -e0 $(CE_BIN) -C $(CE_CFG) -v > $$1 2>&1 & srv=$$!; \
	  for i in $$(seq 1 25); do curl -s -m 2 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CE_HPORT)/ 2>/dev/null | grep -q 200 && break; sleep 1; done; \
	  for i in $$(seq 1 15); do python3 -c "import socket;s=socket.create_connection(('127.0.0.1',$(CE_CPORT)),2);s.close()" 2>/dev/null && break; sleep 1; done; }; \
	kill_srv() { kill -9 $$srv 2>/dev/null; sleep 1; pkill -9 -f "^$(CE_BIN) -C $(CE_CFG) -v$$" 2>/dev/null; sleep 1; }; \
	\
	echo "=== ce scenario 1: baseline (no gates) ==="; \
	{ printf 'HTTP PORT: $(CE_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP TITLE: mcs-ce\n'; \
	  printf 'TELNET PORT: $(CE_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nSTATS-WINDOW: 2000\nCACHE PORT: 15704\nCACHE FILTER: OFF\n\n'; \
	  printf 'CCCAM PORT: $(CE_CPORT)\nF: cxu cxp { cacheex_mode=3; }\n\n'; \
	  printf '[ cxg ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(CE_NPORT)\nENABLE CACHEEX: YES\nDCW TIMEOUT: 9000\nUSER: u1 p1\n'; } > $(CE_CFG); \
	start_srv .ce-s1.log; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CE_NPORT) u1 p1 $(CE_KEY) 0 1884 64 > .ce-nc1.out 2>&1 & nc=$$!; \
	sleep 2; \
	./$(CXPEER) 127.0.0.1 $(CE_CPORT) cxu cxp "push 1884 0 64 $(CE_GOOD)" > .ce-peer.out 2>&1; \
	wait $$nc; \
	grep -q "\[ ok \] cccam login" .ce-peer.out && echo "  [ ok ] the cacheex peer logged in through the real handshake" || { echo "  [FAIL] cacheex peer login"; ok=0; }; \
	grep -q "DELIVERED A CONTROL WORD" .ce-nc1.out && echo "  [ ok ] baseline: the good push answered the pending ECM" || { echo "  [FAIL] baseline: push not delivered"; ok=0; }; \
	python3 $(CEDCW) 127.0.0.1 $(CE_TPORT) admin admin > .ce-stats.out; \
	sed 's/^/  /' .ce-stats.out | head -9; \
	grep -q "^cacheex-local-only 0$$" .ce-stats.out && grep -q "^cacheex-fake-cw 0$$" .ce-stats.out && grep -q "^cacheex-confirm-wait 0$$" .ce-stats.out && echo "  [ ok ] baseline: all three gate counters at zero" || { echo "  [FAIL] baseline: a gate counted without being armed"; ok=0; }; \
	kill_srv; \
	\
	echo "=== ce scenario 2: gates armed ==="; \
	{ printf 'HTTP PORT: $(CE_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP TITLE: mcs-ce\n'; \
	  printf 'TELNET PORT: $(CE_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nSTATS-WINDOW: 2000\nCACHE PORT: 15704\nCACHE FILTER: OFF\n\n'; \
	  printf 'DCWFILTER CHECKSUM: OFF\nDCWFILTER REPEAT: OFF\n\n'; \
	  printf 'DEFAULT CACHEEX LOCAL_ONLY: YES\nDEFAULT CACHEEX BLOCK_FAKE_CW: YES\nDEFAULT CACHEEX CWCHECK: 2\n\n'; \
	  printf 'CCCAM PORT: $(CE_CPORT)\nF: cxu cxp { cacheex_mode=3; }\n\n'; \
	  printf '[ cxg ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(CE_NPORT)\nENABLE CACHEEX: YES\nDCW TIMEOUT: 9000\nUSER: u1 p1\n'; } > $(CE_CFG); \
	start_srv .ce-s2.log; \
	./$(CXPEER) 127.0.0.1 $(CE_CPORT) cxu cxp "push 1884 0 C8 $(CE_GOOD)" > .ce-peer.out 2>&1; \
	grep -q "\[ ok \] push" .ce-peer.out && echo "  [ ok ] the unsolicited push was sent by the peer" || { echo "  [FAIL] the peer could not push"; ok=0; }; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CE_NPORT) u1 p1 $(CE_KEY) 0 1884 64 > .ce-nc2.out 2>&1 & nc=$$!; \
	sleep 2; \
	./$(CXPEER) 127.0.0.1 $(CE_CPORT) cxu cxp "push 1884 0 64 $(CE_GOOD) bad" "sleep 500" "push 1884 0 64 $(CE_GOOD)" "sleep 500" "push 1884 0 64 $(CE_GOOD)" > .ce-peer.out 2>&1; \
	wait $$nc; \
	grep -q "DELIVERED A CONTROL WORD" .ce-nc2.out && echo "  [ ok ] the confirmed push crossed the floor and was served" || { echo "  [FAIL] the confirmed push never served the client"; ok=0; }; \
	python3 $(CEDCW) 127.0.0.1 $(CE_TPORT) admin admin > .ce-stats.out; \
	sed 's/^/  /' .ce-stats.out | head -9; \
	grep -q "^cacheex-local-only 1$$" .ce-stats.out && echo "  [ ok ] LOCAL_ONLY dropped the unsolicited push" || { echo "  [FAIL] LOCAL_ONLY counter"; ok=0; }; \
	grep -q "^cacheex-fake-cw 1$$" .ce-stats.out && echo "  [ ok ] BLOCK_FAKE_CW dropped the corrupted key" || { echo "  [FAIL] BLOCK_FAKE_CW counter"; ok=0; }; \
	grep -q "^cacheex-confirm-wait 1$$" .ce-stats.out && echo "  [ ok ] CWCHECK held the first unconfirmed arrival" || { echo "  [FAIL] CWCHECK counter"; ok=0; }; \
	kill_srv; \
	\
	echo "=== ce scenario 3: parser notices ==="; \
	{ printf 'HTTP PORT: $(CE_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	  printf 'TELNET PORT: $(CE_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DEFAULT CACHEEX MAXHOP_LG: 4\nDEFAULT CACHEEX CWCHECK: 9\n\n'; \
	  printf '[ cxg ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(CE_NPORT)\nENABLE CACHEEX: YES\nDCW TIMEOUT: 9000\nUSER: u1 p1\n'; } > $(CE_CFG); \
	start_srv .ce-s3.log; \
	grep -q "MAXHOP_LG: not implemented" .ce-s3.log && echo "  [ ok ] MAXHOP_LG says it is not implemented" || { echo "  [FAIL] MAXHOP_LG stayed silent"; ok=0; }; \
	grep -q "CWCHECK capped at 5" .ce-s3.log && echo "  [ ok ] CWCHECK 9 clamped to the cap 5" || { echo "  [FAIL] CWCHECK was not clamped"; ok=0; }; \
	kill_srv; \
	\
	if [ "$$ok" = "1" ] && [ -z "$$CE_KEEP" ]; then rm -f $(CE_CFG) .ce-s1.log .ce-s2.log .ce-s3.log .ce-nc1.out .ce-nc2.out .ce-peer.out .ce-stats.out; \
	elif [ "$$ok" = "1" ]; then echo "  [note] logs kept under tests/.ce-* (CE_KEEP=1)"; \
	else echo "[FAIL] logs kept under tests/.ce-*"; fi; \
	[ "$$ok" = "1" ] || exit 1

$(CXPEER): cxpeer.c ../src/sha1.c ../src/sha1.h ../src/md5.c
	$(CC) -O2 -m64 -std=gnu89 -Wall -o $@ cxpeer.c

# ---------------------------------------------------------------------------
# cy -- TASK R2 (D56): the delivery-time cycle gate.
#
# cachepeer (CSP, fwd=1) answers the server's request with a 30-byte reply
# whose buf[29] DECLARES the half the key belongs to (CP_CYCLE_MARK). The
# profile declares its convention with `SID LIST: 0064.81` (the live anchor:
# cs_accept_ecm -> accept_sid fills ecm->cw1cycle), so with the client's tag
# 0x80 the live rule cwcy_expect() expects CW0 (1):
#
#   baseline : DCWFILTER CYCLE unset -- a key DECLARING the wrong half is
#              still delivered (stock) and the counter stays zero;
#   armed    : DCWFILTER CYCLE: YES -- the wrong-half key is refused
#              (cycle-contradiction 1, the ecm runs to decode-failed), then
#              an agreeing key (mark 1) crosses and is delivered, counter
#              unchanged.
#
# Needs the stats release (the counter is MCS_DCWSTATS-gated).
# ---------------------------------------------------------------------------
CY_BIN    ?= ../bin/multics-r82a-stats-x64
CY2_HPORT = 15710
CY2_TPORT = 15711
CY2_NPORT = 15712
CY2_CACHE = 15713
CY2_P1    = 15714
CY2_P2    = 15715
CY2_CFG   = .cy2-srv.cfg
CY2_KEY   = 0102030405060708091011121314

.PHONY: cy

cy: $(CACHEPEER) $(NCCLIENT)
	@test -x $(CY_BIN) || { echo "build the stats release first: make -C ../make-x64 release-stats"; exit 1; }
	@chmod +x $(CACHEPEER) $(NCCLIENT) 2>/dev/null || true
	@rm -f .cy2-srv.log .cy2-cli.log .cy2-peer.log .cy2-stats.out $(CY2_CFG); ok=1; peer=0; \
	start_srv() { stdbuf -o0 -e0 $(CY_BIN) -C $(CY2_CFG) -v > .cy2-srv.log 2>&1 & srv=$$!; \
	  for i in $$(seq 1 25); do curl -s -m 2 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(CY2_HPORT)/ 2>/dev/null | grep -q 200 && break; sleep 1; done; \
	  sleep 2; }; \
	kill_srv() { kill -9 $$srv 2>/dev/null; sleep 1; pkill -9 -x multics 2>/dev/null; sleep 1; }; \
	start_peer() { \
	  [ "$$peer" != "0" ] && { kill -9 $$peer 2>/dev/null; sleep 0.5; }; \
	  rm -f .cy2-peer.log; \
	  env CP_CYCLE_MARK=$$1 stdbuf -o0 -e0 ./$(CACHEPEER) $(CY2_CACHE) $(CY2_P1) "$$2" 64 > .cy2-peer.log 2>&1 & peer=$$!; \
	  for i in $$(seq 1 90); do grep -q "advertised card" .cy2-peer.log && break; sleep 0.5; done; \
	  sleep 1; }; \
	\
	echo "=== cy scenario 1: baseline (gate unset) ==="; \
	{ printf 'HTTP PORT: $(CY2_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP TITLE: mcs-cy2\n'; \
	  printf 'TELNET PORT: $(CY2_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nSTATS-WINDOW: 2000\nCACHE PORT: $(CY2_CACHE)\nCACHE FILTER: OFF\n\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(CY2_P1) { csp=1; fwd=1 }\n\n'; \
	  printf '[ cy2 ]\nCAID: 1884\nPROVIDERS: 0\nSID LIST: 0064.81 0065.81\nPORT: $(CY2_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 4000\nUSER: u1 p1\nUSER: u2 p1\n'; } > $(CY2_CFG); \
	start_srv; start_peer 2; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CY2_NPORT) u1 p1 $(CY2_KEY) 0 1884 64 > .cy2-cli.log 2>&1; \
	grep -q "decode-failed" .cy2-cli.log && echo "  [ ok ] baseline: stock r82a already refuses the wrong-half key at ingest (client saw decode-failed)" || { echo "  [FAIL] baseline: unexpected delivery"; ok=0; }; \
	grep -q "CYCLE GATE" .cy2-srv.log && { echo "  [FAIL] baseline: the gate spoke while disarmed"; ok=0; } || echo "  [ ok ] baseline: disarmed gate stays silent (stock refusal is invisible)"; \
	python3 cedcw.py 127.0.0.1 $(CY2_TPORT) admin admin > .cy2-stats.out; \
	grep -q "^cycle-contradiction 0$$" .cy2-stats.out && echo "  [ ok ] baseline: cycle-contradiction 0 (stock has no accounting)" || { echo "  [FAIL] baseline: the counter moved without the gate"; ok=0; }; \
	kill_srv; kill -9 $$peer 2>/dev/null; \
	\
	echo "=== cy scenario 2: gate armed ==="; \
	{ printf 'HTTP PORT: $(CY2_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP TITLE: mcs-cy2\n'; \
	  printf 'TELNET PORT: $(CY2_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n'; \
	  printf 'DCW STATS: ON\nSTATS-WINDOW: 2000\nCACHE PORT: $(CY2_CACHE)\nCACHE FILTER: OFF\n\n'; \
	  printf 'DCWFILTER CYCLE: YES\n\n'; \
	  printf 'CACHE PEER: 127.0.0.1:$(CY2_P1) { csp=1; fwd=1 }\nCACHE PEER: 127.0.0.1:$(CY2_P2) { csp=1; fwd=1 }\n\n'; \
	  printf '[ cy2 ]\nCAID: 1884\nPROVIDERS: 0\nSID LIST: 0064.81 0065.81\nPORT: $(CY2_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 4000\nUSER: u1 p1\nUSER: u2 p1\n'; } > $(CY2_CFG); \
	start_srv; start_peer 2; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CY2_NPORT) u1 p1 $(CY2_KEY) 0 1884 64 > .cy2-cli.log 2>&1; \
	grep -q "decode-failed" .cy2-cli.log && echo "  [ ok ] armed: the wrong-half key never reached the client (decode-failed)" || { echo "  [FAIL] armed: the client saw a key"; ok=0; }; \
	grep -q "CYCLE GATE" .cy2-srv.log && echo "  [ ok ] armed: the refusal is logged with its reason" || { echo "  [FAIL] armed: no gate line"; ok=0; }; \
	python3 cedcw.py 127.0.0.1 $(CY2_TPORT) admin admin > .cy2-stats.out; \
	grep -q "^cycle-contradiction 1$$" .cy2-stats.out && echo "  [ ok ] armed: cycle-contradiction 1" || { echo "  [FAIL] armed: counter"; ok=0; }; \
	kill -9 $$peer 2>/dev/null; sleep 1; \
	env CP_CYCLE_MARK=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(CY2_CACHE) $(CY2_P2) 10203060405060F070809080A0B0C010 64 > .cy2-peer2.log 2>&1 & peer2=$$!; \
	for i in $$(seq 1 60); do grep -q "advertised card" .cy2-peer2.log && break; sleep 0.5; done; sleep 1; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CY2_NPORT) u2 p1 $(CY2_KEY) 0 1884 65 > .cy2-cli.log 2>&1; \
	kill -9 $$peer2 2>/dev/null; \
	grep -q "DELIVERED A CONTROL WORD" .cy2-cli.log && echo "  [ ok ] armed: the agreeing key crossed and was served" || { echo "  [FAIL] armed: agreeing key not served"; ok=0; }; \
	python3 cedcw.py 127.0.0.1 $(CY2_TPORT) admin admin > .cy2-stats.out; \
	grep -q "^cycle-contradiction 1$$" .cy2-stats.out && echo "  [ ok ] armed: the agreeing delivery did not count" || { echo "  [FAIL] armed: counter moved on an agreeing key"; ok=0; }; \
	kill_srv; kill -9 $$peer 2>/dev/null; sleep 1; \
	\
	pkill -9 -x .cachepeer.bin 2>/dev/null; sleep 1; \
	if [ "$$ok" = "1" ]; then rm -f $(CY2_CFG) .cy2-srv.log .cy2-cli.log .cy2-peer.log .cy2-peer2.log .cy2-stats.out; \
	else echo "[FAIL] logs kept under tests/.cy2-*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK R3 (D57) -- the login doors: progressive delay + optional allowlist,
# live. Both doors answer a wrong password instantly in stock -- a keyboard
# cracker gets hundreds of guesses a second from one IP with nothing in the
# log but disconnects. The remedy is opt-in:
#
#   HTTP LOGIN DELAY / TELNET LOGIN DELAY   (ms base, 0 = off = stock)
#   HTTP LOGIN ALLOW / TELNET LOGIN ALLOW   (empty = off)
#
# THE SCENARIO. Four servers on their own ports. Phase 1 (AU1, everything
# off) pins the stock floor: three wrong telnet passwords and two wrong
# Basic credentials answer fast and log nothing. Phase 2 (AU2, DELAY 250)
# makes the same guesses pay: 250/500/1000 ms before the failure lines, a
# good login still instant and resetting the count (the next failure is
# 250 again), the http 401s delayed the same way. Phase 3 (AU3 with
# ALLOW 127.0.0.1, AU4 with ALLOW 127.0.0.2) proves both sides of the
# list: the listed address logs in, the stranger's connection closes
# before any credential is read, with one [LOGIN ALLOW] line each.
#
# PORTS: 16900-16907, distinct from every other target.
# ---------------------------------------------------------------------------

AU1_HPORT = 16900
AU1_TPORT = 16901
AU2_HPORT = 16902
AU2_TPORT = 16903
AU3_HPORT = 16904
AU3_TPORT = 16905
AU4_HPORT = 16906
AU4_TPORT = 16907
AU_CFG1   = .au1.cfg
AU_CFG2   = .au2.cfg
AU_CFG3   = .au3.cfg
AU_CFG4   = .au4.cfg

.PHONY: au
au:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x multics 2>/dev/null; sleep 1; true
	@{ printf 'HTTP PORT: $(AU1_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n'; \
	   printf 'HTTP TITLE: mcs-au1\nTELNET PORT: $(AU1_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\n\n'; \
	   printf '[ au1 ]\nNEWCAMD PORT: 0\n'; } > $(AU_CFG1); \
	{ printf 'HTTP PORT: $(AU2_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP LOGIN DELAY: 250\n'; \
	   printf 'HTTP TITLE: mcs-au2\nTELNET PORT: $(AU2_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\nTELNET LOGIN DELAY: 250\n\n'; \
	   printf '[ au2 ]\nNEWCAMD PORT: 0\n'; } > $(AU_CFG2); \
	{ printf 'HTTP PORT: $(AU3_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP LOGIN ALLOW: 127.0.0.1\n'; \
	   printf 'HTTP TITLE: mcs-au3\nTELNET PORT: $(AU3_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\nTELNET LOGIN ALLOW: 127.0.0.1\n\n'; \
	   printf '[ au3 ]\nNEWCAMD PORT: 0\n'; } > $(AU_CFG3); \
	{ printf 'HTTP PORT: $(AU4_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\nHTTP LOGIN ALLOW: 127.0.0.2\n'; \
	   printf 'HTTP TITLE: mcs-au4\nTELNET PORT: $(AU4_TPORT)\nTELNET USER: admin\nTELNET PASS: admin\nTELNET LOGIN ALLOW: 127.0.0.2\n\n'; \
	   printf '[ au4 ]\nNEWCAMD PORT: 0\n'; } > $(AU_CFG4); \
	ok=1; rm -f .au*-srv.log; \
	stdbuf -o0 -e0 $(BIN) -C $(AU_CFG1) -v > .au1-srv.log 2>&1 & srv1=$$!; \
	for i in $$(seq 1 40); do curl -s -m 2 -o /dev/null -u admin:admin http://127.0.0.1:$(AU1_HPORT)/ && break; sleep 1; done; \
	e1=$$(python3 autelnet.py $(AU1_TPORT) admin wrong); \
	e2=$$(python3 autelnet.py $(AU1_TPORT) admin wrong); \
	e3=$$(python3 autelnet.py $(AU1_TPORT) wrong admin); \
	h1=$$(curl -s -m 5 -o /dev/null -w '%{time_total}' -u admin:bad http://127.0.0.1:$(AU1_HPORT)/); \
	h2=$$(curl -s -m 5 -o /dev/null -w '%{time_total}' -u admin:bad http://127.0.0.1:$(AU1_HPORT)/); \
	sum=$$(echo "$$e1 $$e2 $$e3" | python3 -c "import sys; print(sum(int(p.split()[3]) for p in sys.stdin.read().splitlines() if p.strip()))"); \
	if echo "$$e1 $$e2 $$e3" | grep -qv "denied"; then echo "[FAIL] phase 1: a wrong login was not denied ($$e1 $$e2 $$e3)"; ok=0; \
	else echo "  [ ok ] phase 1: wrong logins are denied"; fi; \
	if [ "$$sum" -lt 2000 ]; then echo "  [ ok ] phase 1: three wrong logins took $${sum}ms total -- the stock door answers instantly"; \
	else echo "[FAIL] phase 1: wrong logins cost $${sum}ms with the throttle OFF"; ok=0; fi; \
	fast=$$(python3 -c "print(1 if float('$$h1')<1.0 and float('$$h2')<1.0 else 0)"); \
	if [ "$$fast" = "1" ]; then echo "  [ ok ] phase 1: wrong Basic credentials get instant 401s"; \
	else echo "[FAIL] phase 1: http failures took $$h1/$$h2 s with the throttle OFF"; ok=0; fi; \
	if grep -q "LOGIN THROTTLE\|LOGIN ALLOW" .au1-srv.log; then echo "[FAIL] phase 1: a throttle line appeared with everything OFF"; ok=0; \
	else echo "  [ ok ] phase 1: the log stays silent (off = stock, byte for byte)"; fi; \
	stdbuf -o0 -e0 $(BIN) -C $(AU_CFG2) -v > .au2-srv.log 2>&1 & srv2=$$!; \
	for i in $$(seq 1 40); do curl -s -m 2 -o /dev/null -u admin:admin http://127.0.0.1:$(AU2_HPORT)/ && break; sleep 1; done; \
	t1=$$(python3 autelnet.py $(AU2_TPORT) admin wrong | sed -n 's/.*ELAPSED //p'); \
	t2=$$(python3 autelnet.py $(AU2_TPORT) admin wrong | sed -n 's/.*ELAPSED //p'); \
	t3=$$(python3 autelnet.py $(AU2_TPORT) wrong admin | sed -n 's/.*ELAPSED //p'); \
	if [ "$$t1" -ge 200 ] && [ "$$t2" -ge 450 ] && [ "$$t3" -ge 950 ]; then echo "  [ ok ] phase 2: the guesses paid 250/$${t1}ms, 500/$${t2}ms, 1000/$${t3}ms (base 250, doubling)"; \
	else echo "[FAIL] phase 2: the delays did not double ($$t1/$$t2/$$t3, want >=200/450/950)"; ok=0; fi; \
	good=$$(python3 autelnet.py $(AU2_TPORT) admin admin); \
	if echo "$$good" | grep -q "^RESULT ok" ; then \
	  gt=$$(echo "$$good" | sed -n 's/.*ELAPSED //p'); \
	  if [ "$$gt" -lt 1500 ]; then echo "  [ ok ] phase 2: the right password logs in right away ($${gt}ms)"; \
	  else echo "[FAIL] phase 2: the right password took $${gt}ms"; ok=0; fi; \
	else echo "[FAIL] phase 2: the right password was rejected ($$good)"; ok=0; fi; \
	t4=$$(python3 autelnet.py $(AU2_TPORT) admin wrong | sed -n 's/.*ELAPSED //p'); \
	if [ "$$t4" -lt 900 ]; then echo "  [ ok ] phase 2: a good login reset the count -- the next typo is 250ms again ($${t4}ms)"; \
	else echo "[FAIL] phase 2: the count did not reset (4th failure $${t4}ms, want <900)"; ok=0; fi; \
	ht1=$$(curl -s -m 5 -o /dev/null -w '%{time_total}' -u admin:bad http://127.0.0.1:$(AU2_HPORT)/); \
	hc=$$(curl -s -m 5 -o /dev/null -w '%{http_code}' -u admin:admin http://127.0.0.1:$(AU2_HPORT)/); \
	if python3 -c "exit(0 if float('$$ht1')>=0.2 else 1)"; then echo "  [ ok ] phase 2: the http door pays too ($${ht1}s for the first wrong Basic)"; \
	else echo "[FAIL] phase 2: http failure answered in $$ht1 s"; ok=0; fi; \
	if [ "$$hc" = "200" ]; then echo "  [ ok ] phase 2: correct http credentials still open the page"; \
	else echo "[FAIL] phase 2: correct http credentials answered '$$hc'"; ok=0; fi; \
	if grep -q "failure #3 from" .au2-srv.log && grep -q "delaying 1000ms" .au2-srv.log; then echo "  [ ok ] phase 2: the log said why -- failure #3, delaying 1000ms"; \
	else echo "[FAIL] phase 2: no [LOGIN THROTTLE] line with the reason"; ok=0; fi; \
	stdbuf -o0 -e0 $(BIN) -C $(AU_CFG3) -v > .au3-srv.log 2>&1 & srv3=$$!; \
	stdbuf -o0 -e0 $(BIN) -C $(AU_CFG4) -v > .au4-srv.log 2>&1 & srv4=$$!; \
	for i in $$(seq 1 40); do curl -s -m 2 -o /dev/null -u admin:admin http://127.0.0.1:$(AU3_HPORT)/ && break; sleep 1; done; \
	inr=$$(python3 autelnet.py $(AU3_TPORT) admin admin); \
	if echo "$$inr" | grep -q "^RESULT ok"; then echo "  [ ok ] phase 3: the listed address logs in"; \
	else echo "[FAIL] phase 3: the listed address was refused ($$inr)"; ok=0; fi; \
	ins=$$(curl -s -m 5 -o /dev/null -w '%{http_code}' -u admin:admin http://127.0.0.1:$(AU3_HPORT)/); \
	if [ "$$ins" = "200" ]; then echo "  [ ok ] phase 3: and opens the http page"; \
	else echo "[FAIL] phase 3: listed address got '$$ins' on http"; ok=0; fi; \
	outr=$$(python3 autelnet.py $(AU4_TPORT) admin admin); \
	if echo "$$outr" | grep -q "closed"; then echo "  [ ok ] phase 3: the stranger's telnet connection closes before any credential"; \
	else echo "[FAIL] phase 3: the stranger reached the login ($$outr)"; ok=0; fi; \
	outc=$$(curl -s -m 5 -o /dev/null -w '%{http_code}' -u admin:admin http://127.0.0.1:$(AU4_HPORT)/ 2>/dev/null; true); \
	if [ "$$outc" != "200" ]; then echo "  [ ok ] phase 3: the stranger's http request never becomes a page ('$$outc')"; \
	else echo "[FAIL] phase 3: the unlisted address got the page"; ok=0; fi; \
	if grep -q "LOGIN ALLOW.*not on the allow list" .au4-srv.log; then echo "  [ ok ] phase 3: the refusal is logged with the address"; \
	else echo "[FAIL] phase 3: no [LOGIN ALLOW] line"; ok=0; fi; \
	if grep -q "not on the allow list" .au3-srv.log; then echo "[FAIL] phase 3: the compliant server logged an allowlist refusal"; ok=0; \
	else echo "  [ ok ] phase 3: the compliant server's log is clean"; fi; \
	kill $$srv1 $$srv2 $$srv3 $$srv4 2>/dev/null; sleep 1; \
	for s in $$srv1 $$srv2 $$srv3 $$srv4; do kill -9 $$s 2>/dev/null; done; \
	pkill -9 -x multics 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(AU_CFG1) $(AU_CFG2) $(AU_CFG3) $(AU_CFG4) .au*-srv.log; \
	else echo "[FAIL] logs kept under tests/.au*"; fi; \
	[ "$$ok" = "1" ] || exit 1


# ---------------------------------------------------------------------------
# TASK R4 (D58) -- the persistent peer-reputation ladder, live.
#
# THE SCENARIO. One server, two cache peers on one profile (SID LIST pairs
# = "these channels declare CW0"): peer A agrees (mark 1), peer B poisons
# (mark 2 = the other half). DCWFILTER CYCLE arms the counted ingest
# refusal, so every marked wrong-half key from B is a CONFIRMED, logged
# event. Thresholds are small for the test: distrust 3, isolate 6, ban 10.
#
#   phase 1  A alone: the honest find is served (the baseline mesh works)
#   phase 2  B joins; its first request-reply is confirmed event #1 and
#            arms its repush; the flood of marked keys climbs the ledger --
#            the third event escalates to DISTRUST and the file pins it
#   phase 3  a fresh channel's find is answered by A only (distrust means
#            we stop asking); events 4..6 still land, the sixth escalates
#            to ISOLATE; the pushes are refused at the door but the
#            arrivals themselves count, and the tenth crosses to BAN --
#            FLAG_DISABLE, pings stop, the ledger freezes
#   phase 4  restart: the file re-arms the ban the moment B shows up
#            ("ban restored"); the mesh still serves through A
#   phase 5  recovery: the operator deletes B's line from the file and
#            restarts -- both peers are asked again, the mesh is whole
#
# PORTS: 16920-16925, distinct from every other target.
# ---------------------------------------------------------------------------

PR_HPORT  = 16920
PR_TPORT  = 16921
PR_CACHE  = 16922
PR_A      = 16923
PR_B      = 16924
PR_NPORT  = 16925
PR_CFG    = .pr-srv.cfg
PR_KEY    = 0102030405060708091011121314
PR_CW     = 10203060405060F070809080A0B0C010
PR_REP    = .pr.rep

.PHONY: pr
pr:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; sleep 1; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; true
	@rm -f $(PR_REP) $(PR_CFG) .pr-srv.log .pr-a.log .pr-b.log .pr-cli.log multics.peers; \
	{ printf 'HTTP PORT: $(PR_HPORT)\nHTTP USER: a\nHTTP PASS: a\nHTTP TITLE: mcs-pr\nTELNET PORT: $(PR_TPORT)\nTELNET USER: a\nTELNET PASS: a\nDCW STATS: ON\n'; \
	   printf 'CACHE PORT: $(PR_CACHE)\nCACHE FILTER: OFF\n'; \
	   printf 'PEER REPUTATION: ON\nPEER REPUTATION FILE: $(PR_REP)\nPEER REPUTATION DISTRUST: 3\nPEER REPUTATION ISOLATE: 6\nPEER REPUTATION BAN: 10\n\n'; \
	   printf '[ pr ]\nCAID: 1884\nPROVIDERS: 0\nSID LIST: 0064.81 0065.81 0066.81 0067.81\nPORT: $(PR_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 4000\nDCWFILTER CYCLE: YES\nUSER: u1 p1\n\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(PR_A) { csp=1; fwd=1 }\nCACHE PEER: 127.0.0.1:$(PR_B) { csp=1; fwd=1 }\n'; } > $(PR_CFG); \
	ok=1; \
	stdbuf -o0 -e0 $(BIN) -C $(PR_CFG) -v > .pr-srv.log 2>&1 & srv=$$!; \
	sleep 3; \
	env CP_CYCLE_MARK=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(PR_CACHE) $(PR_A) "$(PR_CW)" 64 > .pr-a.log 2>&1 & pa=$$!; \
	for i in $$(seq 1 60); do grep -q "advertised card" .pr-a.log && break; sleep 0.5; done; \
	for i in $$(seq 1 40); do grep -q "come Online" .pr-srv.log && break; sleep 0.5; done; sleep 2; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PR_NPORT) u1 p1 $(PR_KEY) 0 1884 64 > .pr-cli.log 2>&1; \
	grep -q "DELIVERED A CONTROL WORD" .pr-cli.log && echo "  [ ok ] phase 1: the honest find is served by the lone honest peer" || { echo "[FAIL] phase 1: the find was not served"; ok=0; }; \
	env CP_CYCLE_MARK=2 CP_REPUSH_MS=250 stdbuf -o0 -e0 ./$(CACHEPEER) $(PR_CACHE) $(PR_B) "$(PR_CW)" 64 > .pr-b.log 2>&1 & pb=$$!; \
	for i in $$(seq 1 30); do grep -q "got request" .pr-b.log && break; NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PR_NPORT) u1 p1 $(PR_KEY) 0 1884 65 > /dev/null 2>&1; sleep 3; done; \
	for i in $$(seq 1 20); do grep -q "got request" .pr-b.log && break; NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PR_NPORT) u1 p1 $(PR_KEY) 0 1884 66 > /dev/null 2>&1; sleep 3; done; \
	for i in $$(seq 1 40); do grep -q "ESCALATED to DISTRUST" .pr-srv.log && break; sleep 0.5; done; \
	if grep -q "ESCALATED to DISTRUST" .pr-srv.log; then echo "  [ ok ] phase 2: the third confirmed event escalated to distrust"; else echo "[FAIL] phase 2: no distrust escalation"; ok=0; fi; \
	pst=$$(grep -oE "^127.0.0.1:$(PR_B) [0-9]" $(PR_REP) | sed 's/.* //'); \
	if [ -n "$$pst" ]; then echo "  [ ok ] phase 2: the file carries B's record (stage $$pst at check time -- the 250 ms flood keeps climbing past distrust)"; else echo "[FAIL] phase 2: the file does not carry B"; ok=0; fi; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PR_NPORT) u1 p1 $(PR_KEY) 0 1884 67 > /dev/null 2>&1; \
	sleep 7; \
	areq=$$(grep -c "got request" .pr-a.log); breq=$$(grep -c "got request" .pr-b.log); \
	if [ "$$areq" -ge 3 ] && [ "$$breq" = "1" ]; then echo "  [ ok ] phase 3: the fresh channel was asked to A only (A $$areq, B $$breq) -- distrust means we stop asking"; else echo "[FAIL] phase 3: the distrusted peer still gets requests (A $$areq, B $$breq)"; ok=0; fi; \
	for i in $$(seq 1 60); do grep -q "ESCALATED to ISOLATE" .pr-srv.log && break; sleep 0.5; done; \
	if grep -q "ESCALATED to ISOLATE" .pr-srv.log; then echo "  [ ok ] phase 3: the sixth event escalated to isolate (4 and 5 still landed: distrust listens)"; else echo "[FAIL] phase 3: no isolate escalation"; ok=0; fi; \
	for i in $$(seq 1 60); do grep -q "dropped unprocessed" .pr-srv.log && break; sleep 0.5; done; \
	if grep -q "dropped unprocessed" .pr-srv.log; then echo "  [ ok ] phase 3: the isolated peer's pushes are refused at the door"; else echo "[FAIL] phase 3: isolated pushes were still processed"; ok=0; fi; \
	for i in $$(seq 1 90); do grep -q "ESCALATED to BAN" .pr-srv.log && break; sleep 0.5; done; \
	if grep -q "ESCALATED to BAN" .pr-srv.log; then echo "  [ ok ] phase 3: keeping the pushes coming walked B to ban (arrivals counted, reason signed)"; else echo "[FAIL] phase 3: the ban never came"; ok=0; fi; \
	if grep -q "^127.0.0.1:$(PR_B) 3 " $(PR_REP); then echo "  [ ok ] phase 3: the file pinned the ban (stage 3)"; else echo "[FAIL] phase 3: the file does not pin the ban"; ok=0; fi; \
	pings_before=$$(grep -c "got MultiCS ping" .pr-b.log); sleep 4; pings_after=$$(grep -c "got MultiCS ping" .pr-b.log); \
	if [ "$$pings_before" = "$$pings_after" ]; then echo "  [ ok ] phase 3: the pings to the banned peer stopped"; else echo "[FAIL] phase 3: the banned peer is still being pinged ($$pings_before -> $$pings_after)"; ok=0; fi; \
	ev1=$$(grep -oE "^127.0.0.1:$(PR_B) 3 [0-9]+" $(PR_REP) | grep -oE "[0-9]+$$"); sleep 3; ev2=$$(grep -oE "^127.0.0.1:$(PR_B) 3 [0-9]+" $(PR_REP) | grep -oE "[0-9]+$$"); \
	if [ -n "$$ev1" ] && [ "$$ev1" = "$$ev2" ]; then echo "  [ ok ] phase 3: the ledger froze at the ban ($$ev1 events, drops past FLAG_DISABLE are stock-silent)"; else echo "[FAIL] phase 3: the ledger moved past the ban ($$ev1 -> $$ev2)"; ok=0; fi; \
	kill -9 $$pa $$pb 2>/dev/null; kill $$srv 2>/dev/null; sleep 2; kill -9 $$srv 2>/dev/null; \
	stdbuf -o0 -e0 $(BIN) -C $(PR_CFG) -v >> .pr-srv.log 2>&1 & srv=$$!; sleep 3; \
	env CP_CYCLE_MARK=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(PR_CACHE) $(PR_A) "$(PR_CW)" 64 >> .pr-a.log 2>&1 & pa=$$!; \
	env CP_CYCLE_MARK=2 CP_REPUSH_MS=250 stdbuf -o0 -e0 ./$(CACHEPEER) $(PR_CACHE) $(PR_B) "$(PR_CW)" 64 >> .pr-b.log 2>&1 & pb=$$!; \
	for i in $$(seq 1 60); do grep -q "ban restored from the reputation file" .pr-srv.log && break; sleep 0.5; done; \
	if grep -q "ban restored from the reputation file" .pr-srv.log; then echo "  [ ok ] phase 4: after the restart the file re-arms the ban, said once"; else echo "[FAIL] phase 4: the ban did not survive the restart"; ok=0; fi; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PR_NPORT) u1 p1 $(PR_KEY) 0 1884 64 > .pr-cli.log 2>&1; \
	if grep -q "DELIVERED A CONTROL WORD" .pr-cli.log; then echo "  [ ok ] phase 4: the mesh still serves through the honest peer"; else echo "[FAIL] phase 4: serving broke after the restart"; ok=0; fi; \
	kill -9 $$pa $$pb 2>/dev/null; kill $$srv 2>/dev/null; sleep 2; kill -9 $$srv 2>/dev/null; \
	grep -v "^127.0.0.1:$(PR_B) " $(PR_REP) > $(PR_REP).new && mv $(PR_REP).new $(PR_REP); \
	stdbuf -o0 -e0 $(BIN) -C $(PR_CFG) -v >> .pr-srv.log 2>&1 & srv=$$!; sleep 3; \
	env CP_CYCLE_MARK=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(PR_CACHE) $(PR_A) "$(PR_CW)" 64 >> .pr-a.log 2>&1 & pa=$$!; \
	env CP_CYCLE_MARK=2 CP_SHORT_REPLY=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(PR_CACHE) $(PR_B) "$(PR_CW)" 64 >> .pr-b.log 2>&1 & pb=$$!; \
	for i in $$(seq 1 60); do grep -q "advertised card" .pr-a.log && grep -q "advertised card" .pr-b.log && break; sleep 0.5; done; sleep 4; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(PR_NPORT) u1 p1 $(PR_KEY) 0 1884 64 > .pr-cli.log 2>&1; \
	if grep -q "DELIVERED A CONTROL WORD" .pr-cli.log; then echo "  [ ok ] phase 5: after the operator removed the line, the mesh is whole again"; else echo "[FAIL] phase 5: recovery failed"; ok=0; fi; \
	breq2=$$(grep -c "got request" .pr-b.log); \
	if [ "$$breq2" -ge 1 ]; then echo "  [ ok ] phase 5: the recovered peer receives finds again (B $$breq2)"; else echo "[FAIL] phase 5: the recovered peer is still shunned"; ok=0; fi; \
	kill -9 $$pa $$pb 2>/dev/null; kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; \
	pkill -9 -x multics 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(PR_REP) $(PR_CFG) .pr-srv.log .pr-a.log .pr-b.log .pr-cli.log multics.peers; \
	else echo "[FAIL] logs kept under tests/.pr*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK R5 (D59) -- the machine readout: /json, live.
#
# One server (ports 16940-16945), Basic auth on, DCW STATS ON, the R4
# ladder armed at 3/6/10. The document must carry the six sections, the
# stranger must pay the same 401 every page pays, an online peer must
# appear as a row, and a live poisoning flood must surface BOTH as a
# peerrep record AND merged into the peer's row -- the same evidence the
# /cache page and telnet PEERREP show, machine-readable for Grafana.
# ---------------------------------------------------------------------------

JM_HPORT  = 16940
JM_TPORT  = 16941
JM_CACHE  = 16942
JM_A      = 16943
JM_B      = 16944
JM_NPORT  = 16945
JM_CFG    = .jm-srv.cfg
JM_REP    = .jm.rep

.PHONY: jm
# HOP-LOOP NOTES (keep this text ABOVE the recipe -- a comment line inside
# a backslash-continued recipe breaks the shell chain and orphans $$ok and
# $$srv for the epilogue): the loop hops a FRESH sid every try because a
# repeated ecmd5 is served from the entry and never re-fans
# (CACHE_FLAG_REQSENT), and a peer only becomes askable after its first
# ping round-trip, so 20 tries walk the 14 declared channels 0x64-0x71
# (decimal 100-113 -- printf %02X of the DECIMAL base). The flood peer
# seeds its push identity from its FIRST find only, and its 150 ms marked
# repushes need a few seconds to pile up the three contradiction events
# phase 8 reads -- hence the break on B's own 'got request' + sleep 8.
jm:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 -x multics 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; sleep 1; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; true
	@rm -f $(JM_REP) $(JM_CFG) .jm-srv.log .jm-a.log .jm-b.log .jm.json multics.peers; \
	{ printf 'HTTP PORT: $(JM_HPORT)\nHTTP USER: a\nHTTP PASS: a\nHTTP TITLE: mcs-jm\nTELNET PORT: $(JM_TPORT)\nTELNET USER: a\nTELNET PASS: a\nDCW STATS: ON\n'; \
	   printf 'CACHE PORT: $(JM_CACHE)\nCACHE FILTER: OFF\n'; \
	   printf 'PEER REPUTATION: ON\nPEER REPUTATION FILE: $(JM_REP)\nPEER REPUTATION DISTRUST: 3\nPEER REPUTATION ISOLATE: 6\nPEER REPUTATION BAN: 10\n\n'; \
	   printf '[ jm ]\nCAID: 1884\nPROVIDERS: 0\nSID LIST: 0064.81 0065.81 0066.81 0067.81 0068.81 0069.81 006A.81 006B.81 006C.81 006D.81 006E.81 006F.81 0070.81 0071.81\nPORT: $(JM_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 4000\nDCWFILTER CYCLE: YES\nUSER: u1 p1\n\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(JM_A) { csp=1; fwd=1 }\nCACHE PEER: 127.0.0.1:$(JM_B) { csp=1; fwd=1 }\n'; } > $(JM_CFG); \
	ok=1; \
	stdbuf -o0 -e0 $(BIN) -C $(JM_CFG) -v > .jm-srv.log 2>&1 & srv=$$!; \
	sleep 3; \
	code1=$$(curl -s -o /dev/null -w '%{http_code}' -u wrong:wrong http://127.0.0.1:$(JM_HPORT)/json); \
	if [ "$$code1" = "401" ]; then echo "  [ ok ] phase 1: the stranger's /json is the same 401 every page pays"; else echo "[FAIL] phase 1: /json answered $$code1 without credentials"; ok=0; fi; \
	curl -s -u a:a http://127.0.0.1:$(JM_HPORT)/json > .jm.json; \
	if python3 -c "import json; d=json.load(open('.jm.json')); assert set(['version','uptime','now','dcwstats','peerrep','cache_peers']) <= set(d)"; then echo "  [ ok ] phase 2: the document parses and carries the six sections"; else echo "[FAIL] phase 2: the document is not valid JSON with the six sections"; ok=0; fi; \
	if python3 -c "import json; d=json.load(open('.jm.json')); s=d['dcwstats']; names=['accepted','checksum','null/half-null','repeat-3-bytes','bad-dcw-list','cacheex-local-only','cacheex-fake-cw','cacheex-confirm-wait','cycle-contradiction','consensus-mismatch']; assert s['on'] in (0,1) and all(n in s for n in names) and all(isinstance(s[n],int) for n in names)"; then echo "  [ ok ] phase 3: the dcwstats section carries every named counter as integers"; else echo "[FAIL] phase 3: the dcwstats section is incomplete"; ok=0; fi; \
	if python3 -c "import json; d=json.load(open('.jm.json')); p=d['peerrep']; assert p['on']==1 and p['thresholds']=={'distrust':3,'isolate':6,'ban':10} and p['peers']==[]"; then echo "  [ ok ] phase 4: the ladder section shows the armed thresholds and an empty list"; else echo "[FAIL] phase 4: the ladder section is wrong"; ok=0; fi; \
	env CP_CYCLE_MARK=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(JM_CACHE) $(JM_A) "$(PR_CW)" 64 > .jm-a.log 2>&1 & pa=$$!; \
	for i in $$(seq 1 60); do grep -q "advertised card" .jm-a.log && break; sleep 0.5; done; \
	for i in $$(seq 1 40); do grep -q "come Online" .jm-srv.log && break; sleep 0.5; done; sleep 1; \
	curl -s -u a:a http://127.0.0.1:$(JM_HPORT)/json > .jm.json; \
	if python3 -c "import json; d=json.load(open('.jm.json')); rows=[r for r in d['cache_peers'] if r['port']==$(JM_A)]; assert rows and rows[0]['ping']>0 and rows[0]['rep_stage']==-1"; then echo "  [ ok ] phase 5: the online honest peer is a row with a ping and no ladder record"; else echo "[FAIL] phase 5: the honest peer's row is missing or wrong"; ok=0; fi; \
	env CP_CYCLE_MARK=2 CP_REPUSH_MS=150 stdbuf -o0 -e0 ./$(CACHEPEER) $(JM_CACHE) $(JM_B) "$(PR_CW)" 64 > .jm-b.log 2>&1 & pb=$$!; \
	for i in $$(seq 1 20); do grep -q "got request" .jm-b.log && break; sid=$$(printf '%02X' $$(( 100 + (i % 14) ))); NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(JM_NPORT) u1 p1 $(PR_KEY) 0 1884 0x$$sid > /dev/null 2>&1; sleep 1; done; \
	for i in $$(seq 1 60); do grep -q "ESCALATED to DISTRUST" .jm-srv.log && break; sleep 0.5; done; \
	sleep 8; \
	grep -q "ESCALATED to DISTRUST" .jm-srv.log && echo "  [ ok ] phase 6: the flood really escalated (the evidence is live, not read back)" || { echo "[FAIL] phase 6: no distrust escalation to read"; ok=0; }; \
	curl -s -u a:a http://127.0.0.1:$(JM_HPORT)/json > .jm.json; \
	if python3 -c "import json; d=json.load(open('.jm.json')); recs=[r for r in d['peerrep']['peers'] if r['peer']=='127.0.0.1:$(JM_B)']; assert recs and 1<=recs[0]['stage']<=3 and recs[0]['events']>=3"; then echo "  [ ok ] phase 7: the live ladder record is in the document (distrust or beyond, 3+ events -- the 150 ms flood keeps climbing)"; else echo "[FAIL] phase 7: the live ladder record is missing from the document"; ok=0; fi; \
	if python3 -c "import json; d=json.load(open('.jm.json')); rows=[r for r in d['cache_peers'] if r['port']==$(JM_B)]; st=d['dcwstats']; assert rows and 1<=rows[0]['rep_stage']<=3 and rows[0]['rep_events']>=3 and rows[0]['rep_last'] and (st['on']==0 or st['cycle-contradiction']>=3)"; then echo "  [ ok ] phase 8: the record is merged into the row, and a stats build shows the flood in cycle-contradiction"; else echo "[FAIL] phase 8: the row merge did not happen"; ok=0; fi; \
	kill -9 $$pa $$pb 2>/dev/null; kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(JM_REP) $(JM_CFG) .jm-srv.log .jm-a.log .jm-b.log .jm.json multics.peers; else echo "[FAIL] logs kept under tests/.jm*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK R6 (D60) -- the dual performance measurement for the bounded cache
# queue. Runs the SAME scripted load against whatever binary BIN points at
# and prints one PM: metrics line -- so `make pm` (default) vs
# `make pm BIN=../bin/multics-r82a-queue-stats-x64` (parallel) is the
# apples-to-apples comparison the roadmap asks for.
#
# LOAD. One server, one honest peer answering finds, one flood peer
# repushing rotating (unmarked -- no cycle gate, no reputation here) keys
# every 5 ms (~200 datagrams/s), 100 client ECMs paced at 250 ms (~25 s).
#
# METRICS. delivered / requested, flood datagrams actually sent, server
# CPU (utime+stime ticks from /proc), peak RSS, and -- only on a queue
# binary -- the worker's own [CACHE QUEUE] tally from the log.
# ---------------------------------------------------------------------------

PM_HPORT  = 16980
PM_TPORT  = 16981
PM_CACHE  = 16982
PM_A      = 16983
PM_B      = 16984
PM_NPORT  = 16985
PM_CFG    = .pm-srv.cfg

.PHONY: pm
pm:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 multics 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; sleep 1; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; true
	@rm -f $(PM_CFG) .pm-srv.log .pm-a.log .pm-b.log .pm-cli.log; \
	{ printf 'HTTP PORT: $(PM_HPORT)\nHTTP USER: a\nHTTP PASS: a\nHTTP TITLE: mcs-pm\nTELNET PORT: $(PM_TPORT)\nTELNET USER: a\nTELNET PASS: a\nDCW STATS: ON\n'; \
	   printf 'CACHE PORT: $(PM_CACHE)\nCACHE FILTER: OFF\n\n'; \
	   printf '[ pm ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(PM_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 4000\nUSER: u1 p1\n\n'; \
	   printf 'CACHE PEER: 127.0.0.1:$(PM_A)\nCACHE PEER: 127.0.0.1:$(PM_B)\n'; } > $(PM_CFG); \
	stdbuf -o0 -e0 $(BIN) -C $(PM_CFG) -v > .pm-srv.log 2>&1 & srv=$$!; sleep 3; \
	srvpid=$$srv; \
	utime0=$$(awk '{print $$14+$$15}' /proc/$$srvpid/stat 2>/dev/null); \
	hwm0=$$(grep VmHWM /proc/$$srvpid/status 2>/dev/null | awk '{print $$2}'); \
	env CP_CYCLE_MARK=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(PM_CACHE) $(PM_A) "$(PR_CW)" 64 > .pm-a.log 2>&1 & pa=$$!; \
	for i in $$(seq 1 60); do grep -q "advertised card" .pm-a.log && break; sleep 0.5; done; \
	for i in $$(seq 1 40); do grep -q "come Online" .pm-srv.log && break; sleep 0.5; done; \
	env CP_REPUSH_MS=5 stdbuf -o0 -e0 ./$(CACHEPEER) $(PM_CACHE) $(PM_B) "$(PR_CW)" 64 > .pm-b.log 2>&1 & pb=$$!; \
	for i in $$(seq 1 60); do grep -q "advertised card" .pm-b.log && break; sleep 0.5; done; sleep 1; \
	t0=$$(date +%s%N); \
	NC_ECMS=100 NC_ECM_GAP_MS=250 NC_HOP=1 ./$(NCCLIENT) 127.0.0.1 $(PM_NPORT) u1 p1 $(PR_KEY) 0 1884 64 > .pm-cli.log 2>&1; \
	t1=$$(date +%s%N); \
	sleep 2; \
	utime1=$$(awk '{print $$14+$$15}' /proc/$$srvpid/stat 2>/dev/null); \
	hwm1=$$(grep VmHWM /proc/$$srvpid/status 2>/dev/null | awk '{print $$2}'); \
	deliv=$$(grep -c "DELIVERED A CONTROL WORD" .pm-cli.log); \
	pushes=$$(grep -c "unsolicited push" .pm-b.log); \
	qtally=$$(grep -o "dropped [0-9]*" .pm-srv.log | tail -1 | awk '{print $$2}'); \
	qtally=$${qtally:-0}; \
	wall_ms=$$(( (t1 - t0) / 1000000 )); cpu_ms=$$(( (utime1 - utime0) * 10 )); \
	echo "PM: binary=$(BIN)"; \
	echo "PM: delivered=$$deliv/100  wall_ms=$$wall_ms  pushes_applied=$$pushes  cpu_ms=$$cpu_ms  rss_hwm_kb=$${hwm1:-?}  queue_dropped=$$qtally"; \
	kill -9 $$pa $$pb 2>/dev/null; kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 multics 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; 
	@echo "PM: logs kept under tests/.pm-*"

# ---------------------------------------------------------------------------
# TASK R7 (D61) -- the new skin: INTERNAL css (no second request, no CDN,
# no external reference at all), viewport responsiveness, Arabic/RTL toggle,
# and token sessions that ride on the SAME Basic door (the allowlist still
# closes pre-auth, the delay still taxes real failures; /logout can only
# invalidate the caller's own token). NOTE: keep explanations above the
# recipe -- a tabbed comment inside it breaks the backslash chain.
# ---------------------------------------------------------------------------
SK_HPORT  = 16990
SK_NPORT  = 16991
SK_TPORT  = 16992
SK_CFG    = .sk-srv.cfg
SK_CSS    = .sk-override.css

.PHONY: sk
sk:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1
	@rm -f $(SK_CFG) $(SK_CSS) .sk-*.log .sk-*.html .sk-*.hdr; \
	{ printf 'HTTP PORT: $(SK_HPORT)\nHTTP USER: admin\nHTTP PASS: secret\nTELNET PORT: 0\n\n[ sk ]\nCAID: 1884\nPORT: $(SK_NPORT)\nNEWCAMD PORT: 0\n'; } > $(SK_CFG); \
	ok=1; \
	stdbuf -o0 -e0 $(BIN) -C $(SK_CFG) -v > .sk-srv.log 2>&1 & srv=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10; do curl -s -m 2 -o /dev/null -u admin:secret http://127.0.0.1:$(SK_HPORT)/ && break; sleep 1; done; \
	curl -s -D .sk-login.hdr -o .sk-login.html -u admin:secret http://127.0.0.1:$(SK_HPORT)/; \
	tok=$$(grep -o 'MCSSESSION=[0-9a-f]*' .sk-login.hdr | head -1); \
	if [ -n "$$tok" ] && grep -q "HttpOnly" .sk-login.hdr && grep -q "SameSite=Strict" .sk-login.hdr; then \
	  echo "  [ ok ] one Basic login mints a 256-bit session token (HttpOnly, SameSite=Strict)"; \
	else echo "[FAIL] no session token on login"; ok=0; fi; \
	if grep -q "<style type=" .sk-login.html && grep -q 'name="viewport"' .sk-login.html \
	   && ! grep -q 'rel="stylesheet"' .sk-login.html \
	   && ! grep -qE "(src|href)='https?://" .sk-login.html \
	   && ! grep -qE '(src|href)="https?://' .sk-login.html \
	   && grep -q "Uptime:" .sk-login.html; then \
	  echo "  [ ok ] the skin is internal and self-contained: inline css, viewport, zero external refs"; \
	else echo "[FAIL] skin not self-contained"; ok=0; fi; \
	c1=$$(curl -s -o /dev/null -w '%{http_code}' -H "Cookie: $$tok" http://127.0.0.1:$(SK_HPORT)/cache); \
	if [ "$$c1" = "200" ]; then \
	  echo "  [ ok ] the token alone opens every page -- no password re-send"; \
	else echo "[FAIL] cookie-only request gave $$c1"; ok=0; fi; \
	c2=$$(curl -s -o /dev/null -w '%{http_code}' -H "Cookie: MCSSESSION=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" http://127.0.0.1:$(SK_HPORT)/); \
	c3=$$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:$(SK_HPORT)/); \
	if [ "$$c2" = "401" ] && [ "$$c3" = "401" ]; then \
	  echo "  [ ok ] the door did not move: a forged token and a stranger both take the same 401"; \
	else echo "[FAIL] gates moved (forged $$c2, stranger $$c3)"; ok=0; fi; \
	curl -s -D .sk-logout.hdr -o /dev/null -H "Cookie: $$tok" http://127.0.0.1:$(SK_HPORT)/logout; \
	c4=$$(curl -s -o /dev/null -w '%{http_code}' -H "Cookie: $$tok" http://127.0.0.1:$(SK_HPORT)/); \
	if grep -q "MCSSESSION=;" .sk-logout.hdr && [ "$$c4" = "401" ]; then \
	  echo "  [ ok ] logout expires the cookie and the dead token is refused"; \
	else echo "[FAIL] logout (expire+reuse $$c4)"; ok=0; fi; \
	curl -s -D .sk-ar.hdr -o .sk-ar.html -u admin:secret "http://127.0.0.1:$(SK_HPORT)/?lang=ar"; \
	curl -s -o .sk-ar2.html -H "Cookie: MCSLANG=ar" -u admin:secret http://127.0.0.1:$(SK_HPORT)/threads; \
	if grep -q "dir=rtl lang=ar" .sk-ar.html && grep -q "MCSLANG=ar" .sk-ar.hdr \
	   && grep -q "الرئيسية" .sk-ar.html && grep -q "dir=rtl lang=ar" .sk-ar2.html \
	   && grep -q "الخيوط" .sk-ar2.html; then \
	  echo "  [ ok ] Arabic is a real mode: rtl html, Arabic menu, the cookie carries it across pages"; \
	else echo "[FAIL] the Arabic toggle"; ok=0; fi; \
	curl -s -o .sk-en.html -u admin:secret "http://127.0.0.1:$(SK_HPORT)/?lang=en"; \
	if ! grep -q "<HTML dir=rtl" .sk-en.html && grep -q "Home" .sk-en.html; then \
	  echo "  [ ok ] ?lang=en flips straight back to LTR English"; \
	else echo "[FAIL] the English toggle"; ok=0; fi; \
	scss=$$(curl -s -u admin:secret http://127.0.0.1:$(SK_HPORT)/style.css | head -c 20); \
	case "$$scss" in ":root{"*) echo "  [ ok ] /style.css still serves the builtin skin for legacy tools";; *) echo "[FAIL] /style.css body"; ok=0;; esac; \
	cj=$$(curl -s -o .sk-json.out -w '%{http_code}' -H "Cookie: $$tok" http://127.0.0.1:$(SK_HPORT)/json); \
	if [ "$$cj" = "401" ]; then \
	  echo "  [ ok ] a dead token cannot read /json either (the logout really locked it)"; \
	else echo "[FAIL] /json with dead token gave $$cj"; ok=0; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	sleep 1; \
	{ printf 'HTTP PORT: $(SK_HPORT)\nHTTP USER: admin\nHTTP PASS: secret\nTELNET PORT: 0\nFILE STYLESHEET: "$(SK_CSS)"\n\n[ sk ]\nCAID: 1884\nPORT: $(SK_NPORT)\nNEWCAMD PORT: 0\n'; } > $(SK_CFG); \
	printf '/* operator override */\nbody{background:#000;}\n' > $(SK_CSS); \
	stdbuf -o0 -e0 $(BIN) -C $(SK_CFG) -v > .sk-srv2.log 2>&1 & srv=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10; do curl -s -m 2 -o /dev/null -u admin:secret http://127.0.0.1:$(SK_HPORT)/ && break; sleep 1; done; \
	curl -s -o .sk-ovr.html -u admin:secret http://127.0.0.1:$(SK_HPORT)/; \
	o1=$$(curl -s -u admin:secret http://127.0.0.1:$(SK_HPORT)/style.css | head -c 11); \
	if grep -q 'rel="stylesheet"' .sk-ovr.html && ! grep -q "<style type=" .sk-ovr.html && [ "$$o1" = "/* operator" ]; then \
	  echo "  [ ok ] the stock FILE STYLESHEET override still wins with a plain link"; \
	else echo "[FAIL] stylesheet override"; ok=0; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(SK_CFG) $(SK_CSS) .sk-*.log .sk-*.html .sk-*.hdr .sk-json.out; else echo "[FAIL] logs kept under tests/.sk-*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK R8 (D62) -- the cache consensus arbitrer (CACHE CONSENSUS, default
# OFF). Four live phases across three server generations:
#   P1 gen OFF        -> stock first-wins, zero [CONSENSUS] lines
#   P2 gen ON         -> a lone honest peer is held, then RELEASED at the
#                        window (availability first, only the cache source
#                        waits -- card servers never do)
#   P3 gen ON         -> two honest voters deliver EARLY (before the window)
#   P4 gen ON         -> a peer escalated to distrust on the R4 ladder loses
#                        the weighted decision: the clean key delivers, the
#                        distrusted one is refused + convicted again
# The fake-vs-honest pair differs inside ONE checksum group (bytes 2-3 of
# group 1 swapped) so both keys pass acceptDCW -- only consensus can tell
# them apart. NOTE: keep explanations ABOVE the recipe; no tabbed comments
# inside it (the backslash chain rule).
# ---------------------------------------------------------------------------
CN_HPORT  = 16960
CN_CACHE  = 16961
CN_NPORT  = 16962
CN_CFG    = .cn-srv.cfg
CN_REP    = .cn.rep

.PHONY: cn
cn:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; sleep 1; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; true
	@rm -f $(CN_CFG) $(CN_REP) .cn-*.log .cn-*.cfg .cn-cli*.log; \
	ok=1; \
	echo "  [ -- ] P1: consensus OFF, stock first-wins"; \
	{ printf 'HTTP PORT: $(CN_HPORT)\nHTTP USER: a\nHTTP PASS: a\nTELNET PORT: 0\nDCW STATS: ON\nCACHE PORT: $(CN_CACHE)\nCACHE FILTER: OFF\nCACHE PEER: 127.0.0.1:16971\nCACHE PEER: 127.0.0.1:16972\n\n[ cn ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(CN_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 6000\nUSER: u1 p1\n\n'; } > $(CN_CFG); \
	stdbuf -o0 -e0 $(BIN) -C $(CN_CFG) -v > .cn1-srv.log 2>&1 & srv=$$!; sleep 3; \
	env CP_REPUSH_MS=100 stdbuf -o0 -e0 ./$(CACHEPEER) $(CN_CACHE) 16971 "10302060405060F070809080A0B0C010" 64 > .cn1-f.log 2>&1 & pf=$$!; \
	env CP_REPUSH_MS=100 stdbuf -o0 -e0 ./$(CACHEPEER) $(CN_CACHE) 16972 "10203060405060F070809080A0B0C010" 64 > .cn1-h.log 2>&1 & ph=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10; do grep -q "come Online" .cn1-srv.log && break; sleep 0.5; done; sleep 1; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CN_NPORT) u1 p1 0102030405060708091011121314 0 1884 0x64 > .cn1-cli.log 2>&1; sleep 1; \
	n1=$$(grep -c "DELIVERED A CONTROL WORD" .cn1-cli.log); \
	c1=$$(grep -c "CONSENSUS" .cn1-srv.log); \
	if [ "$$n1" = "1" ] && [ "$$c1" = "0" ]; then \
	  echo "  [ ok ] P1: OFF delivers exactly as stock and says nothing"; \
	else echo "[FAIL] P1 (delivered $$n1, consensus lines $$c1)"; ok=0; fi; \
	kill $$srv $$pf $$ph 2>/dev/null; sleep 1; kill -9 $$srv $$pf $$ph 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	echo "  [ -- ] P2: consensus ON, a lone honest peer is held then released"; \
	{ printf 'HTTP PORT: $(CN_HPORT)\nHTTP USER: a\nHTTP PASS: a\nTELNET PORT: 0\nDCW STATS: ON\nCACHE PORT: $(CN_CACHE)\nCACHE FILTER: OFF\nCACHE CONSENSUS: ON\nCACHE CONSENSUS WINDOW: 600\nCACHE PEER: 127.0.0.1:16972\n\n[ cn ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(CN_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 6000\nUSER: u1 p1\n\n'; } > $(CN_CFG); \
	stdbuf -o0 -e0 $(BIN) -C $(CN_CFG) -v > .cn2-srv.log 2>&1 & srv=$$!; sleep 3; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(CN_CACHE) 16972 "10203060405060F070809080A0B0C010" 64 > .cn2-h.log 2>&1 & ph=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10; do grep -q "come Online" .cn2-srv.log && break; sleep 0.5; done; sleep 1; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CN_NPORT) u1 p1 0102030405060708091011121314 0 1884 0x65 > .cn2-cli.log 2>&1; sleep 3; \
	n2=$$(grep -c "DELIVERED A CONTROL WORD" .cn2-cli.log); \
	h2=$$(grep -c "window expired, single-source cw released" .cn2-srv.log); \
	if [ "$$n2" = "1" ] && [ "$$h2" -ge 1 ]; then \
	  echo "  [ ok ] P2: the lone key is held for corroboration, then released by the window"; \
	else echo "[FAIL] P2 (delivered $$n2, releases $$h2)"; ok=0; fi; \
	kill $$srv $$ph 2>/dev/null; sleep 1; kill -9 $$srv $$ph 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	echo "  [ -- ] P3/P4: two honest voters deliver early; the late fake never lands"; \
	{ printf 'HTTP PORT: $(CN_HPORT)\nHTTP USER: a\nHTTP PASS: a\nTELNET PORT: 0\nDCW STATS: ON\nCACHE PORT: $(CN_CACHE)\nCACHE FILTER: OFF\nCACHE CONSENSUS: ON\nCACHE CONSENSUS WINDOW: 600\nCACHE PEER: 127.0.0.1:16971\nCACHE PEER: 127.0.0.1:16972\nCACHE PEER: 127.0.0.1:16973\n\n[ cn ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(CN_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 6000\nUSER: u1 p1\nUSER: u2 p1\nUSER: u3 p1\nUSER: u4 p1\n\n'; } > $(CN_CFG); \
	stdbuf -o0 -e0 $(BIN) -C $(CN_CFG) -v > .cn3-srv.log 2>&1 & srv=$$!; sleep 3; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(CN_CACHE) 16972 "10203060405060F070809080A0B0C010" 64 > .cn3-h.log 2>&1 & ph=$$!; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(CN_CACHE) 16973 "10203060405060F070809080A0B0C010" 64 > .cn3-h2.log 2>&1 & ph2=$$!; \
	for i in 1 2 3 4 5 6 7 8 9 10; do grep -q "come Online" .cn3-srv.log && break; sleep 0.5; done; sleep 1; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CN_NPORT) u1 p1 0102030405060708091011121314 0 1884 0x66 > .cn3-cli.log 2>&1; sleep 2; \
	n3=$$(grep -c "DELIVERED A CONTROL WORD" .cn3-cli.log); \
	e3=$$(grep -c "window expired" .cn3-srv.log); \
	if [ "$$n3" = "1" ] && [ "$$e3" = "0" ]; then \
	  echo "  [ ok ] P3: two honest voters corroborate and deliver BEFORE the window"; \
	else echo "[FAIL] P3 (delivered $$n3, expired $$e3)"; ok=0; fi; \
	env CP_ANSWER_DELAY_MS=400 stdbuf -o0 -e0 ./$(CACHEPEER) $(CN_CACHE) 16971 "10302060405060F070809080A0B0C010" 64 > .cn3-f.log 2>&1 & pf=$$!; \
	for i in $$(seq 1 50); do grep -c "come Online" .cn3-srv.log | grep -q "^3$$" && break; sleep 0.5; done; sleep 2; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(CN_NPORT) u2 p1 0102030405060708091011121314 0 1884 0x67 > .cn4-cli.log 2>&1; sleep 3; \
	n4=$$(grep -c "DELIVERED A CONTROL WORD" .cn4-cli.log); \
	g4=$$(grep -c "dcw=10203060405060F070809080A0B0C010" .cn4-cli.log); \
	b4=$$(grep -c "dcw=10302060405060F070809080A0B0C010" .cn4-cli.log); \
	h4=$$(grep -c "CONSENSUS] holding cw" .cn3-srv.log); \
	if [ "$$n4" = "1" ] && [ "$$g4" = "1" ] && [ "$$b4" = "0" ] && [ "$$h4" -ge 2 ]; then \
	  echo "  [ ok ] P4: with the fake listening, the clean key still delivered and the fake never landed"; \
	else echo "[FAIL] P4 (delivered $$n4 good $$g4 bad $$b4 holds $$h4)"; ok=0; fi; \
	kill $$srv $$pf $$ph $$ph2 2>/dev/null; sleep 1; kill -9 $$srv $$pf $$ph $$ph2 2>/dev/null; pkill -9 '^multics' 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(CN_CFG) $(CN_REP) .cn-*.log .cn-cli*.log; else echo "[FAIL] logs kept under tests/.cn-*"; fi; \
	[ "$$ok" = "1" ] || exit 1

# ---------------------------------------------------------------------------
# TASK R11 (D65) -- target `vl`: the per-CW verdict ring, live.
#
#   make -C tests vl
#
# Four phases on ports 16830-16833 (unused by every other target):
#   P1  consensus ON, one lone honest push -> a HELD row, then the window
#       release -> a DELIVERED row for the same key; page and /json agree.
#   P2  cn-P4's recipe: the clean key delivers, the delayed fake loses the
#       weighted decision -> a REFUSED row with reason consensus-mismatch.
#   P3  a flood of fresh keys with no waiter -> STORED rows; the ring caps
#       at 100/100 and the server stays alive.
#   P4  the stock binary has no /cwlog route at all: 404 while / is 200.
# The ring is fed only while DCW STATS is ON -- P1-P3 run with it on.
VL_HPORT = 16830
VL_CACHE = 16831
VL_PEER  = 16832
VL_NPORT = 16833
VL_CFG   = .vl-srv.cfg
VL_CLEAN = 10203060405060F070809080A0B0C010
VL_FAKE  = 10302060405060F070809080A0B0C010

.PHONY: vl

# ---------------------------------------------------------------------------
# TASK R12 (D66) -- the stock reply-interference trap, as a target.
#
# Reproduces the R4 discovery on an armed DCWFILTER CYCLE channel: TWO cache
# peers carry the SAME key with DIFFERENT marks. B (wrong mark) answers
# first and is refused at the ingest gate; A (mark matching the rule)
# answers +400 ms. Pre-R12 the refused reply poisoned the key's node with
# DCW_ERROR and the honest reply was swallowed silently at the status
# early-out -- the client starved on a zero-DCW timeout (reproduced live,
# evidence under docs/interference-R12/). Post-R12 the honest reply
# delivers. Asserts are flavour-agnostic (client delivery + the gate line),
# so the wing runs this against the plain dev binary too.
# ---------------------------------------------------------------------------
RI_HPORT = 16955
RI_TPORT = 16956
RI_CACHE = 16957
RI_NPORT = 16958
RI_A     = 16959
RI_B     = 16960
RI_CFG   = .ri-srv.cfg
RI_CW1   = 10203060405060F070809080A0B0C010
RI_CW2   = 11203061405060F070809080A0B0C010
RI_KEY   = 0102030405060708091011121314

.PHONY: ri
ri: .ncclient.bin .cachepeer.bin
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; sleep 1; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; true
	@rm -f $(RI_CFG) .ri-srv.log .ri-a.log .ri-b.log .ri-cli.log; \
	{ printf 'HTTP PORT: $(RI_HPORT)\nHTTP USER: a\nHTTP PASS: a\nTELNET PORT: $(RI_TPORT)\nDCW STATS: ON\nCACHE PORT: $(RI_CACHE)\nCACHE FILTER: OFF\n\n[ ri ]\nCAID: 1884\nPROVIDERS: 0\nSID LIST: 0064.81 0065.81\nPORT: $(RI_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 4000\nDCWFILTER CYCLE: YES\nUSER: u1 p1\nUSER: u2 p1\n\nCACHE PEER: 127.0.0.1:$(RI_A) { csp=1; fwd=1 }\nCACHE PEER: 127.0.0.1:$(RI_B) { csp=1; fwd=1 }\n'; } > $(RI_CFG); \
	ok=1; \
	stdbuf -o0 -e0 $(BIN) -C $(RI_CFG) -v > .ri-srv.log 2>&1 & srv=$$!; \
	sleep 3; \
	env CP_CYCLE_MARK=1 CP_ANSWER_DELAY_MS=400 stdbuf -o0 -e0 ./$(CACHEPEER) $(RI_CACHE) $(RI_A) "$(RI_CW1)" 64 > .ri-a.log 2>&1 & pa=$$!; \
	env CP_CYCLE_MARK=2 stdbuf -o0 -e0 ./$(CACHEPEER) $(RI_CACHE) $(RI_B) "$(RI_CW1)" 64 > .ri-b.log 2>&1 & pb=$$!; \
	for i in $$(seq 1 30); do [ "$$(grep -c 'come Online' .ri-srv.log)" -ge 2 ] && break; sleep 0.5; done; sleep 1; \
	echo "  [ -- ] P1: the trap -- same key, B's wrong mark answers first, A is +400 ms"; \
	NC_ECMS=1 timeout 30 ./$(NCCLIENT) 127.0.0.1 $(RI_NPORT) u1 p1 $(RI_KEY) 0 1884 64 > .ri-cli.log 2>&1; \
	grep -q "DELIVERED A CONTROL WORD" .ri-cli.log || { sleep 2; NC_ECMS=1 timeout 30 ./$(NCCLIENT) 127.0.0.1 $(RI_NPORT) u1 p1 $(RI_KEY) 0 1884 64 >> .ri-cli.log 2>&1; }; \
	ref1=$$(grep -c "wrong-half key refused" .ri-srv.log); \
	if grep -q "DELIVERED A CONTROL WORD" .ri-cli.log && [ "$$ref1" -ge 1 ]; then \
	  echo "  [ ok ] P1: the wrong mark is refused ($$ref1) AND the honest +400 ms reply delivers -- no node poison"; \
	else echo "[FAIL] P1 (refusals $$ref1) -- see .ri-*.log"; ok=0; fi; \
	kill -9 $$pa $$pb 2>/dev/null; sleep 1; \
	env CP_CYCLE_MARK=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(RI_CACHE) $(RI_A) "$(RI_CW2)" 64 >> .ri-a.log 2>&1 & pa=$$!; \
	env CP_CYCLE_MARK=2 stdbuf -o0 -e0 ./$(CACHEPEER) $(RI_CACHE) $(RI_B) "$(RI_CW2)" 64 >> .ri-b.log 2>&1 & pb=$$!; \
	sleep 2; \
	echo "  [ -- ] P2: a fresh key on a fresh channel -- the mesh is whole"; \
	NC_ECMS=1 timeout 30 ./$(NCCLIENT) 127.0.0.1 $(RI_NPORT) u2 p1 $(RI_KEY) 0 1884 65 > .ri-cli.log 2>&1; \
	grep -q "DELIVERED A CONTROL WORD" .ri-cli.log || { sleep 2; NC_ECMS=1 timeout 30 ./$(NCCLIENT) 127.0.0.1 $(RI_NPORT) u2 p1 $(RI_KEY) 0 1884 65 >> .ri-cli.log 2>&1; }; \
	ref2=$$(grep -c "wrong-half key refused" .ri-srv.log); \
	if grep -q "DELIVERED A CONTROL WORD" .ri-cli.log && [ "$$ref2" -ge 2 ]; then \
	  echo "  [ ok ] P2: the second round trips too ($$ref2 refusals total -- per-reply, never per-key)"; \
	else echo "[FAIL] P2 (refusals $$ref2)"; ok=0; fi; \
	kill -9 $$pa $$pb 2>/dev/null; kill $$srv 2>/dev/null; sleep 1; kill -9 $$pa $$pb $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then rm -f $(RI_CFG) .ri-srv.log .ri-a.log .ri-b.log .ri-cli.log; \
	else echo "[FAIL] logs kept under tests/.ri-*"; fi; \
	[ "$$ok" = "1" ]

vl:
	@test -x ../bin/multics-r82a-stats-x64 || { echo "build first: make -C ../make-x64 release-stats"; exit 1; }
	@test -x ../bin/multics-r82a-stock-x64 || { echo "stock release missing: make -C ../make-x64 release"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; pkill -9 -x '.cachepeer.bin' 2>/dev/null; sleep 1; chmod +x .cachepeer.bin .ncclient.bin 2>/dev/null; true
	@rm -f $(VL_CFG) .vl-*.log; \
	ok=1; \
	echo "  [ -- ] P1: a lone honest key is HELD, then DELIVERED by the window"; \
	{ printf 'HTTP PORT: $(VL_HPORT)\nHTTP USER: a\nHTTP PASS: a\nTELNET PORT: 0\nDCW STATS: ON\nCACHE PORT: $(VL_CACHE)\nCACHE FILTER: OFF\nCACHE CONSENSUS: ON\nCACHE CONSENSUS WINDOW: 300\nCACHE PEER: 127.0.0.1:$(VL_PEER)\n\n[ vl ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(VL_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 6000\nUSER: u1 p1\n\n'; } > $(VL_CFG); \
	stdbuf -o0 -e0 ../bin/multics-r82a-stats-x64 -C $(VL_CFG) -v > .vl1-srv.log 2>&1 & srv=$$!; sleep 3; \
	stdbuf -o0 -e0 ./$(CACHEPEER) $(VL_CACHE) $(VL_PEER) "$(VL_CLEAN)" 64 > .vl1-h.log 2>&1 & ph=$$!; \
	for i in $$(seq 1 10); do grep -q "come Online" .vl1-srv.log && break; sleep 0.5; done; sleep 1; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(VL_NPORT) u1 p1 0102030405060708091011121314 0 1884 0x64 > .vl1-cli.log 2>&1; sleep 3; \
	page1=$$(curl -s -m 3 -u a:a http://127.0.0.1:$(VL_HPORT)/cwlog); \
	held1=$$(printf '%s' "$$page1" | grep -c '>held<'); \
	dlvr1=$$(printf '%s' "$$page1" | grep -c '>delivered<'); \
	cw1=$$(printf '%s' "$$page1" | grep -c '10203060405060f070809080a0b0c010'); \
	js1=$$(curl -s -m 3 -u a:a http://127.0.0.1:$(VL_HPORT)/json); \
	jon1=$$(printf '%s' "$$js1" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["cwlog"]["on"])'); \
	jn1=$$(printf '%s' "$$js1" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["cwlog"]["count"])'); \
	if [ "$$held1" -ge 1 ] && [ "$$dlvr1" -ge 1 ] && [ "$$cw1" -ge 1 ] && [ "$$jon1" = "1" ] && [ "$$jn1" -ge 2 ]; then \
	  echo "  [ ok ] P1: held row, delivered row, the key bytes on the page, /json agrees (count $$jn1)"; \
	else echo "[FAIL] P1 (held $$held1, delivered $$dlvr1, cw $$cw1, json on $$jon1 count $$jn1)"; ok=0; fi; \
	kill $$srv $$ph 2>/dev/null; sleep 1; kill -9 $$srv $$ph 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	echo "  [ -- ] P2: the delayed fake loses the weighted decision -> a REFUSED row"; \
	{ printf 'HTTP PORT: $(VL_HPORT)\nHTTP USER: a\nHTTP PASS: a\nTELNET PORT: 0\nDCW STATS: ON\nCACHE PORT: $(VL_CACHE)\nCACHE FILTER: OFF\nCACHE CONSENSUS: ON\nCACHE CONSENSUS WINDOW: 300\nCACHE PEER: 127.0.0.1:$(VL_PEER)\nCACHE PEER: 127.0.0.1:16834\nCACHE PEER: 127.0.0.1:16835\n\n[ vl ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(VL_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 6000\nUSER: u1 p1\n\n'; } > $(VL_CFG); \
	stdbuf -o0 -e0 ../bin/multics-r82a-stats-x64 -C $(VL_CFG) -v > .vl2-srv.log 2>&1 & srv=$$!; sleep 3; \
	env CP_REPUSH_MS=100 stdbuf -o0 -e0 ./$(CACHEPEER) $(VL_CACHE) $(VL_PEER) "$(VL_CLEAN)" 64 > .vl2-h.log 2>&1 & ph=$$!; \
	env CP_REPUSH_MS=100 stdbuf -o0 -e0 ./$(CACHEPEER) $(VL_CACHE) 16834 "$(VL_CLEAN)" 64 > .vl2-h2.log 2>&1 & ph2=$$!; \
	env CP_ANSWER_DELAY_MS=400 stdbuf -o0 -e0 ./$(CACHEPEER) $(VL_CACHE) 16835 "$(VL_FAKE)" 64 > .vl2-f.log 2>&1 & pf=$$!; \
	for i in $$(seq 1 10); do grep -q "come Online" .vl2-srv.log && break; sleep 0.5; done; sleep 1; \
	NC_ECMS=1 ./$(NCCLIENT) 127.0.0.1 $(VL_NPORT) u1 p1 0102030405060708091011121314 0 1884 0x67 > .vl2-cli.log 2>&1; sleep 3; \
	page2=$$(curl -s -m 3 -u a:a http://127.0.0.1:$(VL_HPORT)/cwlog); \
	ref2=$$(printf '%s' "$$page2" | grep -c '>refused<'); \
	mis2=$$(printf '%s' "$$page2" | grep -c 'consensus-mismatch'); \
	if [ "$$ref2" -ge 1 ] && [ "$$mis2" -ge 1 ]; then \
	  echo "  [ ok ] P2: the fake sits on the page as refused / consensus-mismatch"; \
	else echo "[FAIL] P2 (refused $$ref2, mismatch $$mis2)"; ok=0; fi; \
	kill $$srv $$ph $$ph2 $$pf 2>/dev/null; sleep 1; kill -9 $$srv $$ph $$ph2 $$pf 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	echo "  [ -- ] P3: 400 fresh-key answers fill the ring to 100/100"; \
	{ printf 'HTTP PORT: $(VL_HPORT)\nHTTP USER: a\nHTTP PASS: a\nTELNET PORT: 0\nDCW STATS: ON\nCACHE PORT: $(VL_CACHE)\nCACHE FILTER: OFF\nCACHE PEER: 127.0.0.1:$(VL_PEER)\n\n[ vl ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(VL_NPORT)\nCACHE TIMEOUT: 2000\nDCW TIMEOUT: 6000\nDCWFILTER CHECKSUM: OFF\nUSER: u1 p1\n\n'; } > $(VL_CFG); \
	stdbuf -o0 -e0 ../bin/multics-r82a-stats-x64 -C $(VL_CFG) -v > .vl3-srv.log 2>&1 & srv=$$!; sleep 3; \
	env CP_FRESH_KEY=1 stdbuf -o0 -e0 ./$(CACHEPEER) $(VL_CACHE) $(VL_PEER) "$(VL_CLEAN)" 1000000 > .vl3-h.log 2>&1 & ph=$$!; \
	for i in $$(seq 1 10); do grep -q "come Online" .vl3-srv.log && break; sleep 0.5; done; sleep 1; \
	timeout 30 env NC_HOP=1 NC_ECMS=400 NC_ECM_GAP_MS=15 ./$(NCCLIENT) 127.0.0.1 $(VL_NPORT) u1 p1 0102030405060708091011121314 0 1884 0x64 > .vl3-cli.log 2>&1; sleep 2; \
	page3=$$(curl -s -m 3 -u a:a http://127.0.0.1:$(VL_HPORT)/cwlog); \
	full3=$$(printf '%s' "$$page3" | grep -c '100 / 100'); \
	dl3=$$(printf '%s' "$$page3" | grep -c '>delivered<'); \
	js3=$$(curl -s -m 3 -u a:a http://127.0.0.1:$(VL_HPORT)/json); \
	jn3=$$(printf '%s' "$$js3" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["cwlog"]["count"])'); \
	if ! kill -0 $$srv 2>/dev/null; then echo "[FAIL] P3: server died"; ok=0; \
	elif [ "$$full3" -ge 1 ] && [ "$$dl3" -ge 1 ] && [ "$$jn3" = "100" ]; then \
	  echo "  [ ok ] P3: the ring is full (100/100, json count $$jn3) and the server lives"; \
	else echo "[FAIL] P3 (full $$full3, delivered $$dl3, json $$jn3)"; ok=0; fi; \
	kill $$srv $$ph 2>/dev/null; sleep 1; kill -9 $$srv $$ph 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	echo "  [ -- ] P4: the stock flavour has no /cwlog route at all"; \
	{ printf 'HTTP PORT: $(VL_HPORT)\nHTTP USER: a\nHTTP PASS: a\nCACHE PORT: $(VL_CACHE)\n\n[ vl ]\nCAID: 1884\nPROVIDERS: 0\nPORT: $(VL_NPORT)\nUSER: u1 p1\n\n'; } > $(VL_CFG); \
	stdbuf -o0 -e0 ../bin/multics-r82a-stock-x64 -C $(VL_CFG) -v > .vl4-srv.log 2>&1 & srv=$$!; \
	for i in $$(seq 1 20); do code=$$(curl -s -m 2 -u a:a -o /dev/null -w '%{http_code}' http://127.0.0.1:$(VL_HPORT)/); [ "$$code" = "200" ] && break; sleep 0.5; done; \
	cw4=$$(curl -s -m 3 -u a:a -o /dev/null -w '%{http_code}' http://127.0.0.1:$(VL_HPORT)/cwlog); \
	home4=$$(curl -s -m 3 -u a:a -o /dev/null -w '%{http_code}' http://127.0.0.1:$(VL_HPORT)/); \
	if [ "$$cw4" != "200" ] && [ "$$home4" = "200" ]; then \
	  echo "  [ ok ] P4: stock answers 200 on / and does NOT serve /cwlog ($$cw4)"; \
	else echo "[FAIL] P4 (/cwlog $$cw4, / $$home4)"; ok=0; fi; \
	kill $$srv 2>/dev/null; sleep 1; kill -9 $$srv 2>/dev/null; pkill -9 '^multics' 2>/dev/null; \
	if [ "$$ok" = "1" ]; then echo "vl: 4/4 ok"; else echo "vl: FAILED"; exit 1; fi

# ---------------------------------------------------------------------------
# TASK R13 (D67) -- the overlong HTTP header.
#
# parse_http_request() read up to sizeof(buffer) bytes into a 2048-byte stack
# buffer and then wrote the terminator at buffer[size]: a first packet of
# exactly 2048 bytes put one byte of stack outside the array (the POST body
# loop further down had always guarded this; the header path had not). The
# fix reserves one byte in both recv() calls. This target proves the outcome
# that matters: an oversized header is dropped, the server survives with a
# log free of crash markers, and the NEXT request still answers 200.
# ---------------------------------------------------------------------------
OH_HPORT = 16996
OH_NPORT = 16997

.PHONY: oh

oh:
	@test -x $(BIN) || { echo "build first: make -C ../make-x64"; exit 1; }
	@pkill -9 '^multics' 2>/dev/null; sleep 1; true
	@printf 'HTTP PORT: $(OH_HPORT)\nHTTP USER: admin\nHTTP PASS: admin\n\n' > .oh.cfg; \
	printf '[ oh ]\nNEWCAMD PORT: $(OH_NPORT)\nUSER: u1 p1\n' >> .oh.cfg; \
	rm -f .oh-srv.log; \
	$(BIN) -C .oh.cfg > .oh-srv.log 2>&1 & echo $$! > .oh.pid; \
	code=000; for i in $$(seq 1 25); do \
	  code=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(OH_HPORT)/); \
	  [ "$$code" = "200" ] && break; sleep 1; done; \
	ok=1; \
	if [ "$$code" = "200" ]; then echo "  [ ok ] the server answers 200 before the flood"; else echo "[FAIL] HTTP / answered $$code before the flood"; ok=0; fi; \
	fill=$$(head -c 3000 /dev/zero | tr '\0' A); \
	flood=$$(curl -s -m 5 -u admin:admin -H "X-Fill: $$fill" -o /dev/null -w '%{http_code}' http://127.0.0.1:$(OH_HPORT)/ 2>/dev/null || true); \
	echo "  [ note ] the 3000-byte header came back as '$$flood' (000 = dropped, which is the contract)"; \
	alive=0; kill -0 $$(cat .oh.pid) 2>/dev/null && alive=1; \
	if [ "$$alive" = "1" ]; then echo "  [ ok ] the server survived a header larger than its 2048-byte buffer"; else echo "[FAIL] the server died on the overlong header"; ok=0; fi; \
	if grep -qi "segmentation\|SIGSEGV\|AddressSanitizer\|stack-buffer" .oh-srv.log; then echo "[FAIL] the server log shows a crash marker"; ok=0; fi; \
	code2=000; for i in $$(seq 1 10); do \
	  code2=$$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' http://127.0.0.1:$(OH_HPORT)/); \
	  [ "$$code2" = "200" ] && break; sleep 1; done; \
	if [ "$$code2" = "200" ]; then echo "  [ ok ] the next request still answers 200"; else echo "[FAIL] the next request answered $$code2"; ok=0; fi; \
	kill $$(cat .oh.pid) 2>/dev/null; sleep 1; kill -9 $$(cat .oh.pid) 2>/dev/null; pkill -9 '^multics' 2>/dev/null; rm -f .oh.pid .oh.cfg; \
	if [ "$$ok" = "1" ]; then echo "oh: 3/3 ok"; else echo "oh: FAILED"; exit 1; fi
