# MultiCS r82a — x64 binaries (band 118: ONE base build, 2026-11-19)

## ⭐ The delivered binary — ALWAYS named `multics.x64` (R34/D85)

```
File: bin/multics.x64    <- run this one; the name never changes
Build:  make -C make-x64 release-stats
Content: the single base version = stock r82a logic + MCS_DCWSTATS (the
         /cwlog verdict ring) + R22_LAYERS compiled in (band 118).
         Compiled is not armed: the layers' two reject points wait for
         `R22 LAYERS: YES`; with the line absent/OFF they only count.
Alias:  bin/multics-r82a-stats-x64 and dist/multics-r82a-stats-x64 are
        byte-identical copies of this file (md5sum before any decision).
Run:    ./bin/multics.x64 -C multics-full-30w-52e.cfg -b
```

## Band 118 rule (operator's)

One primary version carrying every verification instrument. Snapshot reads
must never have to ask "which flavour was this?" — that question cost the
band-115 decision a full field session (`/user-trace` said only ON/OFF).

`release`, `release-r22`, `release-queue*`, `release-ab-stats` are **A/B
probes**, not deliveries: they exist to answer one numbered question
(stats-on vs stats-off decisions: `test_dcwstats`; R22-compiled vs not:
soak A/B). Nothing in the field should run them.

## Era artifacts (not rebuilt)

`multics-r82a-x64`, `multics-r82a-arm`, `multics-r82a-mipsel`,
`multics-r82a-sh4` are R24-era builds kept for reference; they do NOT
contain R26+. `multics-r82a-stock-x64`, `-r22-x64`, `-queue-x64`,
`-queue-stats-x64`, `-ab-stats-x64` are probe outputs, rebuilt on demand.
`../bin-frozen-D53/` is the frozen D53 pair and must never be touched.
