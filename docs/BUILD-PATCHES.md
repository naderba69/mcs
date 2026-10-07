# MultiCS r82a — Build & portability patches applied (verified)

Toolchain used to verify: **gcc 14.2.0 (Debian 14.2.0-19), GNU Make 4.4.1, x86-64**.

Layout:

    mcs/
      src/        upstream r82a sources; only Makefile dependency lines touched
      make-x64/   Makefile wrapper, port_gnu89.h, test_dcw.c, test_dcwstats.c
      dist/       release binaries (make release / make release-stats)
      docs/       BUILD-PATCHES.md, BLACKSCREEN.md, PATCH-PLAN.md,
                  STATUS.md, DISCUSSION.md

The directory is called make-x64 rather than build/ on purpose: `build` is one
of the names excluded from workspace snapshots, so a directory by that name
silently disappears between sessions. That already cost one rebuild.

Reproduce:  `cd mcs/make-x64 && make -j8`  ->  `x64/multics`
Self-test:  `cd mcs/make-x64 && make test` ->  8/8 pass (runs the real src/dcw.c)

## B1 — Makefile: static pattern rule could never match
Upstream rule was

    %.o: ../%.c Makefile common.h config.h ecmdata.h

For the target `x64/sha1.o` the stem is `x64/sha1`, so the prerequisite became
`../x64/sha1.c`, which does not exist. GNU Make then silently discards the
static pattern rule and reports:

    make: *** No rule to make target 'x64/sha1.o', needed by 'link'.  Stop.

Fixed to:

    $(OUTPUT)/%.o: ../src/%.c | $(OUTPUT)

(`make -d` confirms the rule now matches with stem `sha1`.)

## B2 — Makefile: prerequisites resolved against the build dir
`httpserver.o` and `main.o` listed `httpserver.h`, `main.c`, `th-ecm.c`,
`srv-*.c`, `cli-*.c` without a directory, but the build runs from `make-x64/`
while sources live in `src/`. All prefixed with `../src/`.

## B3 — Makefile: output directory never created
`$(OUTPUT)` is now an order-only prerequisite with `@mkdir -p $(OUTPUT)`.
Without it, `as` fails with `can't create x64/des.o: No such file or directory`.
`.DEFAULT_GOAL := link` re-pins the default goal (the mkdir rule would
otherwise become it).

## B4 — GCC 14 rejects the implicit declarations r82a relies on
Real compile errors observed:

    ../config.c:2329: error: implicit declaration of function 'strptime'
    ../ecmdata.c:206: error: implicit declaration of function 'malloc'
    ../ecmdata.c:264: error: implicit declaration of function 'MD5'

GCC 14 promoted `-Wimplicit-function-declaration` and `-Wint-conversion` to
errors. Instead of editing 84 upstream files, `make-x64/port_gnu89.h` is injected
with `-include` and supplies `<stdlib.h>`, `<time.h>` and `md5.h`.

## B5 — glibc hides strptime()
`config.c` defines only `_GNU_SOURCE`; modern glibc gates `strptime()` behind
the XSI feature macros. `port_gnu89.h` defines `_XOPEN_SOURCE 700`.

## B6 — GCC 10+ defaults to -fno-common
r82a uses tentative definitions that must merge into one common symbol, so
`-fcommon` is passed explicitly.

## B7 — -std=gnu89
r82a declares functions mid-block after statements (C90 violation). `-std=gnu89`
restores the historical behaviour instead of touching the sources.

## Deliberately KEPT
`-fpack-struct` is kept. MultiCS casts raw network buffers onto its structs, so
removing it would change the wire layout and break compatibility with peers.

## Result
    x64/multics: ELF 64-bit LSB pie executable, x86-64, dynamically linked
    $ ./x64/multics -h
    Multi CardServer r82 - by evileyes (http://www.infosat.org)

### Environment fact — `pgrep -f` / `pkill -f` can kill the calling shell

`pgrep -f <pat>` matches the *full command line*, and the shell running the
command has `<pat>` in its own command line. So

```
for p in $(pgrep -f "multics-r82a"); do kill -9 $p; done
```

killed its own bash session and returned nothing. This is the same trap as
`pkill -f`, hit a second time.

Two further traps found in the same investigation:

- `pgrep -x <name>` is useless for these binaries: the kernel truncates process
  names to 15 characters, and `multics-r82a-stock-x64` is longer, so it matches
  nothing and reports "no processes" whether or not any are running.
- `ps -eo cmd | grep "dist/multics"` matches the grep's own command line and
  reports 2 when nothing is running.

**The check above was itself wrong, and was used for several turns before that
was caught.** It matched `*multics-r82a*` against `/proc/PID/exe`. That only ever
matches the binaries in `dist/`. The `smoke`, `dcwfilter` and `stability` targets
all run `make-x64/x64/multics`, whose name is plain `multics` — so the check
reported "clean" every single time while a server from those targets was still
running and holding ports 15500/15501. A leftover process (pid 2640) was found
only by asking `ss -ltnp` who owned the port.

**The reliable check**, corrected:

```
for d in /proc/[0-9]*; do c=$(cat "$d/comm" 2>/dev/null); \
  case "$c" in *multics*) echo "${d#/proc/} $c";; esac; done
ss -ltnp | grep -E '155|152|157'
```

Match the process *name*, not a path fragment you happen to remember, and treat
`ss` as the authority — a port that is still bound is a leftover whatever the
process list appears to say.

**Make traps found while building the stability harness:**

- `include foo.mk` shares ONE namespace with the including Makefile. `CFG` defined
  in `stability.mk` silently replaced `CFG = multics-live.cfg` in `tests/Makefile`,
  so `smoke` booted with the harness config, hit the startup guard, and reported a
  meaningless `HTTP 000`. Prefix variables in any included file.
- A `#` comment inside a recipe is fatal. The backslash continuations join the
  whole recipe into one shell line, so the comment swallows every command after
  it. It broke twice: once leaving a variable empty, once giving
  `/bin/sh: Syntax error: end of file unexpected`.
- Poll for readiness instead of `sleep 6`. `main.c` blocks startup while
  `/proc/loadavg` is high, and the harness itself raises the load, so a fixed
  sleep fails intermittently and looks like a server bug.

### A crypto self-test must use a realistic key, or it tests the wrong cipher

`EuroDes()` (`src/des.c:490-527`) branches on `key1[7]`:

- `key1[7] != 0` → the **Viaccess** path (`v2mask` + single DES)
- `key1[7] == 0` → **Eurocrypt 3-DES**

Real Newcamd keys always have `key[7] == 0` and `key[15] == 0`, because
`des_key_spread()` ends each 8-byte half with `normal[6] << 1` and
`normal[13] << 1`, whose low bit is always 0. A test key such as `0x11 × 16`
therefore exercises the Viaccess path, and a roundtrip through it fails — which
looks exactly like a framing bug in the code under test.

The first version of the ECM roundtrip check reported `des_decrypt = -1` for the
login message too, even though login works over the network. That contradiction,
not the failure itself, is what exposed the bad key. **When a self-test disagrees
with an observation from the real system, distrust the test first.**
