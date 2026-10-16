# MultiCS r82a — x64 binaries (updated R32, 2026-10-16)

Filename kept for compatibility; the contents are the current builds.

## ⭐ The delivered binary — ALWAYS named `multics.x64` (R34/D85)

```
File: bin/multics.x64    <- run this one; the name never changes
MD5: f006bfbbb381c3e071079dd311fb4676
Size: 779 KB
Flavour: full build (stock logic + /cwlog verdict ring)
Build/refresh: make -C make-x64 release-stats   (rewrites bin/multics.x64)
Run: ./bin/multics.x64 -C multics-r24-optimized.cfg -b
     (or start-production.sh, which already points here)
```

## Current (rebuilt every round from src/)

### multics-r82a-stock-x64 — the production binary
```
MD5: af9a5c1e85c1405c6326031efdcc0f37
Size: 767 KB
Build: make -C make-x64 release
```
### multics-r82a-stats-x64 — same, plus the /cwlog verdict ring
```
MD5: f006bfbbb381c3e071079dd311fb4676
Size: 779 KB
Build: make -C make-x64 release-stats
```
Both carry the full R26-R32 stack; every feature is opt-in and OFF by
default (stock r82a behaviour until armed). Arm the stack in one line:
`CACHE PROTECTION: STANDARD` (see `multics-r24-optimized.cfg`).

## Era artifacts (not rebuilt)

`multics-r82a-x64`, `multics-r82a-arm`, `multics-r82a-mipsel`,
`multics-r82a-sh4` are R24-era builds kept for reference. They do NOT
contain R26-R32. `../bin-frozen-D53/` is the frozen D53 pair and must
never be touched.
