# ---------------------------------------------------------------------------
# MultiCS r82a -- modernized build wrapper (x86_64)
#
# The upstream Makefile (../src/Makefile) is included after the dependency
# fixes recorded in docs/BUILD-PATCHES.md (items B1-B3). This wrapper only:
#   * pins the x64/gcc branch (upstream's default branch names its output "x"),
#   * replaces that branch's CFLAGS with a set that compiles on GCC 10..14,
#   * keeps `make` defaulting to the `link` goal.
#
# Usage:
#   make            build          -> x64/multics
#   make test       8 + 19 unit tests against the real ../src/dcw.c
#   make release    -> ../dist/multics-r82a-stock-x64   (stock r82a logic)
#   make release-stats
#                   -> ../dist/multics-r82a-stats-x64   (+ dcwstats counters)
#   make release-dev
#                   -> ../bin/multics-r82a-dev-x64      (persistent unstripped dev build)
#   make distclean
#
# NOTE: this directory is called make-x64 rather than build/ because `build`
# is one of the directory names excluded from workspace snapshots -- a
# directory by that name silently disappears between sessions.
# ---------------------------------------------------------------------------

# Force the x64/gcc branch before including the upstream Makefile.
target := x64

include ../src/Makefile

# the mkdir rule would otherwise become the default goal
.DEFAULT_GOAL := link

# Compatibility flags (see port_gnu89.h for the full rationale)
# R13 (D67): the C++-only options (-fpermissive, -Wno-return-mismatch) are gone;
# this is a C build, so gcc only answered them with two lines of cc1 noise per
# object (48 lines per flavour) that buried the real warnings.
PORTFLAGS = -include port_gnu89.h -std=gnu89 -fcommon \
	-fno-strict-aliasing -Wno-implicit-function-declaration \
	-Wno-int-conversion -Wno-incompatible-pointer-types \
	-Wno-builtin-declaration-mismatch -Wno-stringop-overread \
	-Wno-stringop-truncation -Wno-format-truncation -Wno-unused-result \
	-Wno-pointer-sign -Wno-deprecated-declarations \
	-Wno-implicit-int -Wno-unused-but-set-variable -Wno-unused-variable \
	-Wno-implicit-fallthrough -Wno-dangling-pointer -Wno-array-bounds \
	-Wno-shift-count-overflow -Wno-format

# Override the x64 branch flags with the portable set.
# NOTE: -fpack-struct is deliberately KEPT: MultiCS casts raw network buffers
# onto packed structs, so dropping it would change the wire layout and break
# compatibility with peers.
CFLAGS = -O2 -m64 -I../src -I. -fpack-struct $(PORTFLAGS) $(OPTS)
LFLAGS = $(CFLAGS)

DIST = ../dist
STATSFLAGS = $(PORTFLAGS) -DMCS_DCWSTATS

# ---------------------------------------------------------------------------
# TASK R6 (D60) -- the parallel cache flavours: the bounded queue ON.
# The default flavours never define CACHE_QUEUE, so their binaries stay
# byte-identical to the pre-R6 releases (proven by fingerprint).
# ---------------------------------------------------------------------------
QUEUEFLAGS = $(PORTFLAGS) -DCACHE_QUEUE

# dev build with the bounded cache queue (result stays in x64/multics)
.PHONY: queue
queue:
	@rm -rf x64 && $(MAKE) --no-print-directory link PORTFLAGS="$(QUEUEFLAGS)" CACHE_QUEUE=1
	@chmod +x x64/multics

release-queue:
	@rm -rf x64 && $(MAKE) --no-print-directory link PORTFLAGS="$(QUEUEFLAGS)" CACHE_QUEUE=1
	@mkdir -p $(DIST)
	@strip -o $(DIST)/multics-r82a-queue-x64 x64/multics
	@echo "built $(DIST)/multics-r82a-queue-x64 (CACHE_QUEUE: bounded cache queue, D60)"
	@mkdir -p ../bin && cp $(DIST)/multics-r82a-queue-x64 ../bin/
	@echo "persistent copy: ../bin/multics-r82a-queue-x64"
	@nm x64/multics | grep -c cq_ | sed 's/^/  cachequeue symbols (expect 7+): /'
	@rm -rf x64

release-queue-stats:
	@rm -rf x64 && $(MAKE) --no-print-directory link PORTFLAGS="$(STATSFLAGS) -DCACHE_QUEUE" CACHE_QUEUE=1 MCS_DCWSTATS=1
	@mkdir -p $(DIST)
	@strip -o $(DIST)/multics-r82a-queue-stats-x64 x64/multics
	@echo "built $(DIST)/multics-r82a-queue-stats-x64 (CACHE_QUEUE + dcwstats)"
	@mkdir -p ../bin && cp $(DIST)/multics-r82a-queue-stats-x64 ../bin/
	@echo "persistent copy: ../bin/multics-r82a-queue-stats-x64"
	@nm x64/multics | grep -c cq_ | sed 's/^/  cachequeue symbols (expect 7+): /'
	@rm -rf x64

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Release flavours. They differ only by -DMCS_DCWSTATS; test_dcwstats proves
# the accept/reject decision is identical either way.
# ---------------------------------------------------------------------------
release:
	@rm -rf x64 && $(MAKE) --no-print-directory link
	@mkdir -p $(DIST)
	@strip -o $(DIST)/multics-r82a-stock-x64 x64/multics
	@echo "built $(DIST)/multics-r82a-stock-x64"
	@mkdir -p ../bin && cp $(DIST)/multics-r82a-stock-x64 ../bin/
	@echo "persistent copy: ../bin/multics-r82a-stock-x64 (dist/ does not survive workspace snapshots)"
	@nm x64/multics | grep -c dcwstats | sed 's/^/  dcwstats symbols (expect 0): /'

# NOTE the `rm -rf x64` at the END, and why it is not tidiness: this target
# builds every object with -DMCS_DCWSTATS. Leaving those .o files behind means a
# later plain `make` finds them newer than the sources, relinks what is on disk,
# and silently produces a binary that is HALF the stats flavour -- measured,
# 746944 bytes against the stock build's 746880 -- which anything running the
# live targets afterwards would then be testing without knowing. Wiping the
# objects costs one rebuild after a stats release and removes the whole class of
# mistake. (A stamp-based guard inside the link rule was tried first: it forces a
# full recompile on every `make`, because a prerequisite that never produces a
# file can never be up to date.)
release-stats:
	@rm -rf x64 && $(MAKE) --no-print-directory link PORTFLAGS="$(STATSFLAGS)" MCS_DCWSTATS=1
	@mkdir -p $(DIST)
	@strip -o $(DIST)/multics-r82a-stats-x64 x64/multics
	@echo "built $(DIST)/multics-r82a-stats-x64"
	@mkdir -p ../bin && cp $(DIST)/multics-r82a-stats-x64 ../bin/
	@echo "persistent copy: ../bin/multics-r82a-stats-x64"
	@nm x64/multics | grep -c dcwstats | sed 's/^/  dcwstats symbols (expect 2): /'
	@rm -rf x64

# Development binary used by the live suite. Keep it unstripped for debugging,
# but persist it outside the snapshot-excluded x64/ build directory so release
# flavour rebuilds do not make tests depend on an ephemeral artifact.
release-dev:
	@rm -rf x64 && $(MAKE) --no-print-directory link
	@mkdir -p ../bin && cp x64/multics ../bin/multics-r82a-dev-x64
	@echo "persistent dev copy: ../bin/multics-r82a-dev-x64"
	@rm -rf x64

# ---------------------------------------------------------------------------
# Self-tests: both exercise the REAL ../src/dcw.c, never a re-implementation.
# ---------------------------------------------------------------------------
# test_peerrep RAN here again as of R9 (D63). The old build-only note blamed
# "in-process state pollution" for a segfault when run right after test_monjson
# -- a misdiagnosis: separate processes never share memory. The real root cause
# was the test's own bare fopen()s: with /tmp/pr-unit absent, fopen returned
# NULL and the first fprintf crashed. The test now mkdirs its directory and
# guards its fixtures (xopen), so running it right after test_monjson is kept
# as the standing regression for exactly that folklore.
test: x64/test_m1align x64/test_cachequeue x64/test_cwconsensus x64/test_monjson x64/test_peerrep x64/test_cwlog x64/test_loginthrottle x64/test_dcw x64/test_dcwstats x64/test_cache_relay x64/test_dcwfilter x64/test_dcwstruct x64/test_cwentropy x64/test_cwtime x64/test_cwplaus x64/test_cwcycle x64/test_cwneg x64/test_cwcm x64/test_trustagg x64/test_cacheguard x64/test_trustlife x64/test_htmlesc x64/test_debugcore x64/test_phase1cfg x64/test_ledger x64/test_peer_origin x64/test_cache_threshold x64/test_peercard x64/test_retry_detect x64/test_cw_reuse x64/test_trust x64/test_cache_purge x64/test_rotation x64/test_cachepref x64/test_parser x64/test_cfgfield x64/test_cacheex_gates
	./x64/test_cachequeue
	@echo
	./x64/test_cwconsensus
	@echo
	./x64/test_monjson
	@echo
	./x64/test_peerrep
	@echo
	./x64/test_cwlog
	@echo
	./x64/test_loginthrottle
	@echo
	./x64/test_dcw
	@echo
	./x64/test_dcwstats
	@echo
	./x64/test_cache_relay
	@echo
	./x64/test_dcwfilter
	./x64/test_dcwstruct
	@echo
	./x64/test_cwentropy
	@echo
	./x64/test_cwtime
	@echo
	./x64/test_cwplaus
	@echo
	./x64/test_cwcycle
	@echo
	./x64/test_cwneg
	@echo
	./x64/test_cwcm
	./x64/test_trustagg
	./x64/test_cacheguard
	@./x64/test_trustlife
	@./x64/test_htmlesc
	@./x64/test_debugcore
	@echo
	./x64/test_phase1cfg
	./x64/test_ledger
	@echo
	./x64/test_peer_origin
	@echo
	./x64/test_cacheex_gates
	@echo
	./x64/test_cache_threshold
	@echo
	./x64/test_peercard
	@echo
	./x64/test_retry_detect
	@echo
	./x64/test_cw_reuse
	@echo
	./x64/test_trust
	@echo
	./x64/test_cache_purge
	@echo
	./x64/test_rotation
	@echo
	./x64/test_cachepref
	./x64/test_parser
	./x64/test_cfgfield
	@echo
	./x64/test_m1align

x64/test_dcw: test_dcw.c ../src/dcw.c ../src/dcw.h
	@mkdir -p x64
	$(CC) $(CFLAGS) -DMCS_SELFTEST -o $@ test_dcw.c ../src/dcw.c

# TASK 2.4: exercises the real graded plausibility scan from ../src/cwplaus.h.
# The module reads 16 bytes and a table of plain integers, so it has no struct
# layout or port-header dependency; it is built the plain way on purpose, which
# is itself the assertion that it stayed that way.
x64/test_cwplaus: test_cwplaus.c ../src/cwplaus.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cwplaus.c

# TASK 2.5: the cache key cycle/parity visibility layer (../src/cwcycle.h).
# Observe-only: the suite pins the rule (expectation from the ECM's own tag),
# silence-is-not-guilt, the one-line-per-window restraint, the two separate
# observation points, the table and the sentence. Plain build on purpose,
# like test_cwplaus: the layer has no dependencies beyond string.h/stdint.h.
x64/test_cwcycle: test_cwcycle.c ../src/cwcycle.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cwcycle.c

# TASK 2.6: negative memory (../src/cwneg.h). Pins the identity (key digest +
# channel), the fresh-vs-reproof bookkeeping, the per-key window throttle, LRU
# eviction that never refuses the next proof, and the evidence line. Plain
# build, like its two siblings: the layer needs only string.h/stdint.h.
x64/test_cwneg: test_cwneg.c ../src/cwneg.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cwneg.c

# TASK 2.7: complement-mirror detection (../src/cwcm.h). Pins the scan (six
# free pairs only -- never the checksum bytes), the 2-of-4 sum-break
# arithmetic the layer's claim rests on, the restraint (one mirror key is
# counted, never scored), the pattern, the table and the sentence. Plain
# build, like its siblings.
x64/test_cwcm: test_cwcm.c ../src/cwcm.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cwcm.c

# TASK 2.8: the coarse trust tier (../src/trustagg.h) against trust.h's own
# arithmetic -- the parity of SOFT/PROOF/GOOD, the crossing and its window,
# the fan-out brake's ask rule, GOOD recovery, LRU, the sentence. Plain build.
x64/test_trustagg: test_trustagg.c ../src/trustagg.h ../src/trust.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_trustagg.c

# TASK 2.9: the cache protocol guard (../src/cacheguard.h) -- the per-type
# minimums against what the handlers really read, the window throttle, the
# sentences, degenerate arguments. Plain build.
x64/test_cacheguard: test_cacheguard.c ../src/cacheguard.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cacheguard.c

# TASK 2.10: the trust lifecycle (../src/trustlife.h) against the restore
# entry points in trust.h / trustagg.h / cwneg.h -- the fade, the strict
# snapshot parser, the restores. Plain build.
x64/test_trustlife: test_trustlife.c ../src/trustlife.h ../src/trust.h ../src/trustagg.h ../src/cwneg.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_trustlife.c

# TASK 3.2: the HTML escape (../src/htmlesc.h) for the web interface's client
# names -- the five metacharacters, honest names byte-for-byte, bounds,
# degenerates, and the end-to-end cell build. Plain build.
x64/test_htmlesc: test_htmlesc.c ../src/htmlesc.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_htmlesc.c

# TASK 3.3: the logging core (../src/debugcore.h) -- clamp-copy into the
# ring, absurd lines, degenerates, and the ring semantics the /debug page
# walks. Plain build.
x64/test_debugcore: test_debugcore.c ../src/debugcore.h ../src/debug.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_debugcore.c

# TASK 1.7: exercises the real TRUSTED-CACHE-FIRST policy from ../src/cachepref.h,
# driven by the real trust engine from ../src/trust.h so the scores it consumes
# are the ones a deployment actually produces.
x64/test_cachepref: test_cachepref.c ../src/cachepref.h ../src/trust.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cachepref.c

# TASK 1.6: exercises the real rotation table from ../src/rotation.h
x64/test_rotation: test_rotation.c ../src/rotation.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -fpack-struct -o $@ test_rotation.c

# TASK 1.5: exercises the real purge-by-origin marks from ../src/cache_purge.h
# -fpack-struct is mandatory here, not cosmetic: the real build uses it, and it
# changes sizeof(struct purge_entry) from 24 to 19. Without it this test would
# measure and pin a layout the server is never compiled with.
x64/test_cache_purge: test_cache_purge.c ../src/cache_purge.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -fpack-struct -o $@ test_cache_purge.c

# TASK 1.4: exercises the real trust engine from ../src/trust.h
x64/test_trust: test_trust.c ../src/trust.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_trust.c

# TASK 1.3: exercises the real CW-reuse detector from ../src/cw_reuse.h
x64/test_cw_reuse: test_cw_reuse.c ../src/cw_reuse.h
	@mkdir -p x64
	# -fpack-struct matches the real build; without it sizeof(entry) reads 48, not 45.
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -fpack-struct -o $@ test_cw_reuse.c

# TASK 1.2: exercises the real retry classifier from ../src/retry_detect.h
x64/test_retry_detect: test_retry_detect.c ../src/retry_detect.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_retry_detect.c

# TASK 1.11a: exercises the real min-peer floor from ../src/cache_threshold.h
x64/test_cache_threshold: test_cache_threshold.c ../src/cache_threshold.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cache_threshold.c

# TASK 3.17: the real card-list search, including a 1024-slot list.
x64/test_peercard: test_peercard.c ../src/peer_cards.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_peercard.c

# TASK 1.1: exercises the real CW-origin model from ../src/peer_origin.h
x64/test_peer_origin: test_peer_origin.c ../src/peer_origin.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_peer_origin.c

# TASK R1: the exchange-gate decision table (pure header, no src objects)
x64/test_cacheex_gates: test_cacheex_gates.c ../src/cacheex_gates.h ../src/peer_origin.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -I../src -o $@ test_cacheex_gates.c

# TASK 1.10b: exercises the real acceptDCW() filter gates from ../src/dcw.c.
# Defines MCS_DCWFILTER_NOGLOBALS so the test supplies the two gate variables
# instead of dcw.c, exactly as the dcwstats test handles MCS_DCWSTATS_NOGLOBALS.
x64/test_dcwfilter: test_dcwfilter.c ../src/dcw.c ../src/dcw.h ../src/dcwfilter.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -DMCS_DCWFILTER_NOGLOBALS -o $@ test_dcwfilter.c ../src/dcw.c

# TASK R3 (D57): the login-throttle module with the clock injected
x64/test_loginthrottle: test_loginthrottle.c ../src/loginthrottle.c ../src/loginthrottle.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -DMCS_LT_NOGLOBALS -o $@ test_loginthrottle.c ../src/loginthrottle.c

# TASK R4 (D58): the reputation ledger, ladder and file
x64/test_peerrep: test_peerrep.c ../src/peerrep.c ../src/peerrep.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_peerrep.c ../src/peerrep.c -lpthread
x64/test_cwlog: test_cwlog.c ../src/cwlog.c ../src/cwlog.h ../src/dcwstats.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cwlog.c ../src/cwlog.c -lpthread
x64/test_monjson: test_monjson.c ../src/monjson.c ../src/monjson.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_monjson.c ../src/monjson.c -lpthread
x64/test_cachequeue: test_cachequeue.c ../src/cachequeue.c ../src/cachequeue.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cachequeue.c ../src/cachequeue.c -lpthread
x64/test_cwconsensus: test_cwconsensus.c ../src/cwconsensus.c ../src/cwconsensus.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cwconsensus.c ../src/cwconsensus.c -lpthread

# TASK 1.8: the structural scan. Links the REAL dcw.c, because the scan calls the
# upstream checksumDCW()/isbadDCW() rather than owning a second copy of them.
x64/test_dcwstruct: test_dcwstruct.c ../src/dcw.c ../src/dcw.h ../src/dcwstruct.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_dcwstruct.c ../src/dcw.c

# TASK 2.2: entropy and collision forensics. The header is pure arithmetic over
# 16 bytes with no dependency on the server at all -- which is why this suite
# can be as complete as it is, and why it needs no other object file.
x64/test_cwentropy: test_cwentropy.c ../src/cwentropy.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cwentropy.c

x64/test_cwtime: test_cwtime.c ../src/cwtime.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cwtime.c

# TASK 1.9: the three options are policy, so the policy is what gets tested --
# the tolerance in rotation.h, the age limit in cache_purge.h, the line in
# statsline.h. All real headers, no re-implementation.
x64/test_ledger: test_ledger.c ../src/ledger.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -fpack-struct -o $@ test_ledger.c

# TASK 3.7: the real server compiled under ASan (main/parser/config only --
# the three TUs that own the char[255] parse buffers and the str[255] writer).
# Used by the live `ps` target as a permanent regression guard for the parse
# layer's stack writes. Non-ASan objects link in untouched.
ASAN_LIBS = $(filter-out $(OUTPUT)/main.o,$(filter-out $(OUTPUT)/parser.o,$(filter-out $(OUTPUT)/config.o,$(OBJECTS))))
x64/multics-asan: ../src/main.c ../src/parser.c ../src/config.c $(ASAN_LIBS)
	@mkdir -p x64
	$(CC) -O1 -g -m64 -I../src -I. -fpack-struct -include port_gnu89.h -std=gnu89 -fcommon -fno-strict-aliasing -w $(OPTS) -fsanitize=address -fsanitize-recover=address -o $@ ../src/main.c ../src/parser.c ../src/config.c $(ASAN_LIBS) -lpthread

# TASK 3.9: direct ASan probe for xmlescape's frame. Links the REAL
# httpserver TU (ASan-instrumented, its xmlescape renamed real_xmlescape)
# against this tiny main(). Pre-fix: ASan stack-buffer-overflow in the
# exml frame walk. Post-fix: bounded truncation, exit 0.
XE_PROBE_LIBS = $(filter-out $(OUTPUT)/httpserver.o,$(filter-out $(OUTPUT)/main.o,$(OBJECTS)))
x64/probe_xmlescape: ../src/httpserver.c ../src/main.c probe_xmlescape.c $(XE_PROBE_LIBS)
	@mkdir -p x64
	$(CC) -O1 -g -m64 -I../src -I. -fpack-struct -include port_gnu89.h -std=gnu89 -fcommon -fno-strict-aliasing -w $(OPTS) -fsanitize=address -fsanitize-recover=address -Dxmlescape=real_xmlescape -c ../src/httpserver.c -o x64/httpserver-xe.o
	$(CC) -O1 -g -m64 -fsanitize=address -fsanitize-recover=address -o x64/probe-xe-main.o -c probe_xmlescape.c
	$(CC) -O1 -g -m64 -I../src -I. -fpack-struct -include port_gnu89.h -std=gnu89 -fcommon -fno-strict-aliasing -w $(OPTS) -fsanitize=address -fsanitize-recover=address -Dmain=multics_main -c ../src/main.c -o x64/main-xe.o
	$(CC) -fsanitize=address -fsanitize-recover=address -o $@ x64/probe-xe-main.o x64/httpserver-xe.o x64/main-xe.o $(XE_PROBE_LIBS) -lpthread

# TASK 3.13: call the REAL cardsids_update. Production main.c, main()
# renamed, linked against the normal objects. The live `fl` target runs it.
FL_PROBE_LIBS = $(filter-out $(OUTPUT)/main.o,$(OBJECTS))
x64/probe_cardsids: ../src/main.c probe_cardsids.c $(FL_PROBE_LIBS)
	@mkdir -p x64
	$(CC) $(CFLAGS) -Dmain=multics_main -c ../src/main.c -o x64/main-fl.o
	$(CC) $(CFLAGS) -o x64/probe-fl-main.o -c probe_cardsids.c
	$(CC) $(LFLAGS) -o $@ x64/probe-fl-main.o x64/main-fl.o $(FL_PROBE_LIBS) -lpthread

# TASK R14 (M1): the packed-struct alignment contract. The translation unit is
# built with -fpack-struct (like the server), so the AES_KEY / SHA_CTX /
# MD5Context a test declares already sit under-aligned; the test proves the
# offsets are odd, then runs the FIPS-197 and RFC-1321/3174 vectors through
# them and compares the store path against the direct path. md5.c, crc32.c and
# msg-camd35.c are textually included (as ../src/main.c does at :2111/:2112),
# because md5.c's context is file-static and msg-camd35.c needs the includer's
# aes.h/md5.h.
x64/test_m1align: test_m1align.c ../src/aes.c ../src/aes.h ../src/sha1.c ../src/sha1.h ../src/md5.c ../src/md5.h ../src/crc32.c ../src/msg-camd35.c
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -fpack-struct -o $@ test_m1align.c ../src/aes.c ../src/sha1.c

x64/test_cfgfield: test_cfgfield.c ../src/cfgfield.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -fpack-struct -o $@ test_cfgfield.c

x64/test_parser: test_parser.c ../src/parser.c ../src/parser.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -fpack-struct -o $@ test_parser.c

x64/test_phase1cfg: test_phase1cfg.c ../src/rotation.h ../src/cache_purge.h ../src/statsline.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -fpack-struct -o $@ test_phase1cfg.c

# TASK 1.0b: exercises the real GR10 predicate from ../src/cache_relay.h
x64/test_cache_relay: test_cache_relay.c ../src/cache_relay.h
	@mkdir -p x64
	$(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_cache_relay.c

# Compiles ../src/dcw.c three ways (stats-on, stats-off, baseline) into one
# binary so the instrumented decisions can be diffed against unmodified r82a.
x64/test_dcwstats: test_dcwstats.c ../src/dcw.c ../src/dcw.h ../src/dcwstats.h
	@mkdir -p x64
	$(CC) $(CFLAGS) -o $@ test_dcwstats.c

distclean:
	-rm -rf x64 ../dist ../bin

# TASK 4.1. Prints the x64 CFLAGS so memcheck.sh can reuse them
# without a second copy of the flag list.
print-cflags:
	@printf '%s\n' '$(CFLAGS)'

# R13 (D67): the source list of the linked server, straight from OBJECTS
# (which the upstream Makefile owns). memcheck.sh used to carry its own copy
# of that list and had silently gone stale: it stopped at main.c, so the
# ASan/TSan/valgrind live server could not link at all once peerrep.o,
# cwconsensus.o, monjson.o and loginthrottle.o joined the build. One source
# of truth, and it can never drift again.
print-srcs:
	@printf '%s\n' '$(patsubst %.o,%.c,$(notdir $(OBJECTS)))'

# Official memory checks. Not part of `make test`: sanitizers and
# valgrind are a separate, slower target.
memcheck:
	./memcheck.sh

# TASK 3.8b: direct ASan probe for parse_quotes' bounded copy. Links the
# REAL parser.c (self-contained) against a tiny main() that mimics the two
# dangerous call-site shapes: read_config's char str[255] with a 400-char
# quoted value, and http_file_data's malloc'd url[512] with a 600-char one.
# Pre-fix: "WRITE of size 401" stack-buffer-overflow (and the real server
# died in read_config:2017 on the same shape). Post-fix: capped at size-1,
# NUL-terminated, exit 0.
.PHONY: probe-pq
probe-pq: x64/probe_parsequotes
	@./x64/probe_parsequotes
x64/probe_parsequotes: probe_parsequotes.c ../src/parser.c ../src/parser.h
	@mkdir -p x64
	$(CC) -O1 -g -m64 -std=gnu89 -fpack-struct -fsanitize=address -o $@ probe_parsequotes.c ../src/parser.c

.PHONY: link test distclean release release-stats release-dev release-queue release-queue-stats print-cflags print-srcs memcheck
