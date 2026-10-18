# MultiCS r82a — x64 binaries (updated R54, 2026-10-18)

Filename kept for compatibility; the contents are the current builds.
Checksums below are the stripped files as shipped, and match `MANIFEST-md5`.
The stock flavour is unchanged since R50 (`c18d548`). The stats and queue-stats
flavours were rebuilt in R55 (the cache-path reason label, see CHANGELOG R55).
The queue flavour was rebuilt in R54. A clean clone of `HEAD` gives the same
stripped checksums (checked in R54).

## ⭐ The delivered binary — ALWAYS named `multics.x64` (R34/D85)

```
File: bin/multics.x64    <- run this one; the name never changes
MD5: adff9a41b1ce767fae1ca4ac7d35c101   (834808 bytes, R55)
Flavour: stats build (stock logic + /cwlog ring + DCW counters, D85/R34)
Build/refresh: make -C make-x64 release-stats   (rewrites bin/multics.x64)
Run: ./bin/multics.x64 -C multics-r24-optimized.cfg -b
     (or start-production.sh, which already points here)
```

## Current builds (rebuilt from src/)

### multics-r82a-stock-x64 — the stock build (no DCW counters)
```
MD5: ecb3f2d021b6bbfb71260f10df1615f7   (793816 bytes)
Build: make -C make-x64 release
```
### multics-r82a-stats-x64 — same as multics.x64
```
MD5: adff9a41b1ce767fae1ca4ac7d35c101   (834808 bytes, R55)
Build: make -C make-x64 release-stats
```
### multics-r82a-queue-x64 — bounded cache queue (D60)
```
MD5: 27e6a35efdaeb2c3c0e119a016f02730   (793816 bytes)
Build: make -C make-x64 release-queue
```
### multics-r82a-queue-stats-x64 — bounded cache queue + DCW counters
```
MD5: af95eb51818bdfacf2d53babeaf2c406   (834808 bytes, R55)
Build: make -C make-x64 release-queue-stats
```

Every feature is opt-in and OFF by default (stock r82a behaviour until armed).
Arm the stack in one line: `CACHE PROTECTION: STANDARD` (see `multics-r24-optimized.cfg`).

## Old artifacts (not rebuilt)

`multics-r82a-x64`, `multics-r82a-arm`, `multics-r82a-mipsel`, `multics-r82a-sh4`
are R24-era builds kept for reference. They do NOT contain R26-R32. `multics-r82a-x64`
has no build target in this repo. The arm, mipsel and sh4 builds need cross
toolchains that are not installed here, so they could not be rebuilt in R54.
`../bin-frozen-D53/` is the frozen D53 pair and must never be touched.
