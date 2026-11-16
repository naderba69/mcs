# MultiCS r82a — x64 binaries (updated band 40, 2026-11-14)

Filename kept for compatibility; the contents are the current builds.

## ⭐ The delivered binary — ALWAYS named `multics.x64` (R34/D85)

```
File: bin/multics.x64    <- run this one; the name never changes
MD5: 67ffa76447dd8ff5cfa640ba1604c938
Size: 1045 KB
Flavour: full build (stock logic + /cwlog verdict ring)
Build/refresh: make -C make-x64 release-stats   (rewrites bin/multics.x64)
Run: ./bin/multics.x64 -C multics-r24-optimized.cfg -b
     (or start-production.sh, which already points here)
```

## Current (rebuilt every round from src/)

### multics-r82a-stock-x64 — the production binary
```
MD5: d09ca7aff026f93e978d3755863c2f6c
Size: 1029 KB
Build: make -C make-x64 release
```
### multics-r82a-stats-x64 — same, plus the /cwlog verdict ring
```
MD5: 67ffa76447dd8ff5cfa640ba1604c938
Size: 1037 KB
Build: make -C make-x64 release-stats
```
`bin/multics.x64` is THE main working binary (operator-confirmed
2026-11-14): all live verification from band 36 runs against it.
Both carry the full R26-R35 stack; every feature is opt-in and OFF by
default (stock r82a behaviour until armed). Arm the stack in one line:
`CACHE PROTECTION: STANDARD` (see `multics-r24-optimized.cfg`).

## Era artifacts (not rebuilt)

`multics-r82a-x64`, `multics-r82a-arm`, `multics-r82a-mipsel`,
`multics-r82a-sh4` are R24-era builds kept for reference. They do NOT
contain R26-R32. `../bin-frozen-D53/` is the frozen D53 pair and must
never be touched.
