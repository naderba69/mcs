# MultiCS r82a — x64 binaries (updated R32, 2026-10-16)

Filename kept for compatibility; the contents are the current builds.

## Current (rebuilt every round from src/)

### multics-r82a-stock-x64 — the production binary
```
MD5: 3d2fdfa4930f592c08acd367b3ade764
Size: 771 KB
Build: make -C make-x64 release
```
### multics-r82a-stats-x64 — same, plus the /cwlog verdict ring
```
MD5: 4be83e2c325be87ca12e9f80aecf08e4
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
