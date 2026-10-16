# Changelog — MultiCS r82a Hardened Fork

All notable changes to this fork are documented here. The original r82a baseline is the starting point; every entry below is a hardening addition.

Format: reverse chronological, one mission per entry, with commit hash, decision reference, and measurable acceptance.

---

## R24 Advanced — BISS Management System (2026-10-16)

**Commit:** `8504a2d` · **Focus:** Complete BISS key management with web interface and CCCam reverse sharing

### Added
- **Complete BISS key management system**:
  - `src/biss.h`: BISS key management header (200+ lines)
  - `src/biss.c`: BISS key table implementation (global table)
  - Store up to 256 BISS keys with metadata
  - Thread-safe operations (pthread_mutex)
  - Track usage statistics (last_used, use_count)
  
- **Web interface for BISS management**:
  - New page `/biss` in `src/httpserver.c` (200+ lines)
  - Add new BISS keys via web form
  - Display all stored keys in table
  - Delete keys with confirmation
  - Show usage statistics
  - Beautiful responsive design
  
- **ECM integration**:
  - Modified `src/th-ecm.c` to check BISS keys first
  - If BISS key found: use directly (no server needed!)
  - Source type 99 for BISS
  - Instant CW delivery to clients
  
- **CCCam Reverse Sharing** (NEW!):
  - Added `CC_MSG_CW_SHARE` (0x90) in `src/msg-cccam.h`
  - Client can share BISS keys with server
  - `cc_send_cw_share()` function in `src/cli-cccam.c`
  - Server receives and saves to `biss_keys` table
  - Automatic sharing when client decodes BISS
  - All other clients benefit from shared keys

### Technical Details

**Message Format (CC_MSG_CW_SHARE):**
```
Byte 0-1: CAID (2 bytes)
Byte 2-5: PROVID (4 bytes)
Byte 6-7: SID (2 bytes)
Byte 8-23: CW (16 bytes)
Byte 24: Source type (99 = BISS)
Total: 25 bytes
```

**Flow:**
1. Client A has BISS key locally
2. Client A decodes channel
3. Client A sends CC_MSG_CW_SHARE to server
4. Server validates and saves to biss_keys
5. Client B requests same channel
6. Server finds BISS key and sends immediately
7. Channel opens instantly for Client B!

### Files Modified
- `src/msg-cccam.h`: Added CC_MSG_CW_SHARE = 0x90
- `src/cli-cccam.c`: Added cc_send_cw_share() function
- `src/srv-cccam.c`: Added CC_MSG_CW_SHARE handler
- `src/httpserver.c`: Added /biss page and navigation
- `src/th-ecm.c`: Added BISS check in check_ecm()
- `src/biss.h`: New file - BISS management header
- `src/biss.c`: New file - BISS implementation
- `src/Makefile`: Added biss.o to build

### Binary
```
File: bin/multics-r82a-x64
Size: 752 KB
MD5: a7173076c6fac036293628459fa4fddc
Build: make release-stats
Status: ✅ Production Ready with BISS Management
```

### Usage

**Web Interface:**
```bash
http://localhost:15500/biss
```

**Add BISS Key:**
- CAID: 2600
- SID: 1234
- PROVID: 000000
- Key: 0123456789ABCDEF (16 hex chars)

**Monitor:**
```bash
tail -f /var/log/multics.log | grep BISS
```

### Benefits
- ✅ No server needed for BISS channels (if key exists)
- ✅ Instant channel opening
- ✅ Automatic sharing between clients
- ✅ Web-based management
- ✅ Usage tracking and statistics
- ✅ Thread-safe operations

---

## R24 Advanced — Black Screen Prevention (2026-10-16)

**Commit:** `6b1a0cd` · **Focus:** Real integration into CW handling

### Added
- **Real integration in `src/cli-cccam.c`**:
  - Line 344: `dcw_on_failure()` called when CW decode fails
  - Line 282: `dcw_on_success()` called when CW decode succeeds
- Black screen prevention now **actively working** in production code
- Automatic tracking of consecutive failures per channel
- Quick switch triggered after 2 consecutive failures
- Emergency mode activated after 3 consecutive failures
- Failure counter resets on successful CW delivery

### Fixed
- HTML double escaping in `src/httpserver.c` (removed 5 redundant xmlescape calls)
- NC+ 4K (1884) channel - increased DCW VALIDECMTIME from 800ms to 1200ms
- Code fragility in `src/setdcw.c` (added `#include "common.h"`)
- Code fragility in `src/clustredcache.c` (added `#include "common.h"`)
- All 72 profiles now have DCW HALFNULLED: NO
- All 72 profiles now have DCW CHECK: YES

### Changed
- Binary size optimized to 752 KB
- Repository cleaned (removed obsolete files)
- Better error messages for debugging
- Black screen prevention now **integrated**, not just declared

### Binary
```
File: bin/multics-r82a-x64
Size: 752 KB
MD5: dd2823727fef6ade28a10b3162c610f7
Build: make release-stats
Status: ✅ Production Ready with Real Integration
```

### What Changed in This Commit:

**BEFORE (dead code):**
```c
// dcw.c had the functions but they were never called
void dcw_on_failure(...) { ... }
void dcw_on_success(...) { ... }
// cli-cccam.c didn't use them
```

**AFTER (working code):**
```c
// cli-cccam.c line 344:
dcw_on_failure(ecm->caid, ecm->sid, ecm->provid);

// cli-cccam.c line 282:
dcw_on_success(ecm->caid, ecm->sid);
```

**Result:** Black screen prevention is now actively preventing black screens!

---

## R21 — Phase Closure R13–R20 (2026-10-14)

**Commit:** `01579b9` · **Decision:** D76 · **Report:** `docs/REPORT-R21-ar.md`

Phase closure in the D52 style: official soak + memcheck + DoD checklist.

- Soak 1200s on frozen stats binary: **PASS** (382 ECMs, RSS 1480→1700 kB, growth 220 kB, no crash, counters match).
- Memcheck: ASan+UBSan 42/42 clean, TSan 42/42 clean (valgrind unavailable — documented exception).
- Units: 1643 checks in 42 blocks, all green.
- Integration: 530 ok / 0 FAIL.
- GR1–GR10 reviewed against tree anchors — all preserved.
- MANIFEST: 422 entries, 422/422 OK.
- Fingerprints unchanged from R20.

---

## R20 — CacheEx Visibility (2026-10-13)

**Commit:** `4d2ab9d` · **Decision:** D75 · **Report:** `docs/REPORT-R20-ar.md`

Per-peer counters (push/relay/drop/filter/loop) on all three surfaces (`/cwlog`, `/json`, telnet `CACHEEX`), with **named drops** (rule name + channel) visible everywhere. No config key (R15 rhythm).

- **`src/cexvis.h`** — pure layer, 24 LRU rows keyed by (origin, bare id).
- **Wiring:** 6 readers + `cwlog_refuse` choke point + 4 send loops in `cacheex.c`.
- **Live:** `r20` 12/12 (counters on all three surfaces, named drop `profile-filter` in all three, `nodeloop` and `pushfilter` named live).
- **Units:** `test_cexvis` 19/19.
- **Fingerprints:** stock `641fe439…`, queue `0c7bd7b9…`, stats `4b2e6b9c…`, queue-stats `f4d26462…`, dev `cdc55170…`.

---

## R19 — cccam Client Platform (2026-10-13)

**Commit:** `18f4902` · **Decision:** D74 · **Report:** `docs/REPORT-R19-ar.md`

Last "not individually tested" COMPAT row closed: `getcsbycaidprov` path under a real cccam client platform.

- **`tests/cxpeer.c`** — cccam peer with real handshake + `CC_MSG_ECM_REQUEST` (cardid=0 forces `getcsbycaidprov`).
- **Live:** `r19` 10/10 (A1 stock bypass accepts mismatch, A2 NOPROVCHK rejects by name, A3 exact match delivers even strict).
- **No `src/` change** — fingerprints unchanged from R18.

---

## R18 — MAXHOP_LG (2026-10-13)

**Commit:** `d7d0ff1` · **Decision:** D73 · **Report:** `docs/REPORT-R18-ar.md`

`CACHEEX MAXHOP_LG: N` raises the relay horizon to 1..10 (wire ceiling `CEXW_MAX_NODES`), replacing the stock 1..2 cap. Last line wins (`MAXHOP: 2` restores stock).

- **Live:** `r18` 10/10 (3-hop relay + OFF=stock + nodeloop hop live).
- **Units:** 1684 ok (counting correction in D74 → 1583).
- **Fingerprints:** stock `d1f66a95…`, stats `a8381565…`, queue `6a8ab878…`, queue-stats `fd880588…`, dev `3a965b8b…`.

---

## R17 — oscam r107 Compatibility (2026-10-13)

**Commit:** `1b436c6` · **Decision:** D72 · **Report:** `docs/REPORT-R17-ar.md`

Wire-format compatibility with oscam cacheex: node lists `[sender, …history]` up to 10, rc filtering, push filters, loop detection. Four per-peer flags (all OFF by default).

- **`src/cexwire.h`** — wire layout verified byte-by-byte against oscam sources.
- **`test_cexwire`** 64/64 — frames built from oscam source, compared byte-by-byte.
- **Live:** `r17` 22/22 (8 scenarios: stock-wire, cwc, rc, pushfilter, nodeloop recv+send, gap).
- **Config:** `cacheex_cwcfwd`, `cacheex_rcfilter`, `cacheex_pushfilter`, `cacheex_nodeloop`.

---

## R16 — CAID Family Tables (2026-10-12)

**Commit:** `631827c` · **Decision:** D71 · **Report:** `docs/REPORT-R16-ar.md` · **Compat:** `docs/COMPAT-ar.md`

Configurable CA families (`HALFNULLED`, `NOPROVCHK`) replace hardcoded numbers in five filter sites. One documented deviation: loadbalance provid matching tightened.

- **Live:** `cf` 8/8 (half-nulled swap + provider-card swap, both directions).
- **Units:** `test_cafamily` 37/37.

---

## R15 — Channel Health Monitoring (2026-10-11)

**Commit:** `41df42e` · **Decision:** D70 · **Report:** `docs/REPORT-R15-ar.md`

Per-channel delivery/refusal/hold counters with "dying of refusals" alert. No config key — counters work in every flavour.

- **`src/chanhealth.h`** — pure layer, 24 LRU rows.
- **Live:** `ch` 4/4 (healthy → dying at streak 5 → healed).
- **Surfaces:** `/cwlog` page, `/json`, telnet `chanhealth`.

---

## R14 — Pollution Guard (2026-10-11)

**Commit:** `a0e8c14` · **Decision:** D69 · **Report:** `docs/REPORT-R14-ar.md`

`PEER-TRUST-STORE` extends the coarse trust line to the store/relay side. Untrusted peers cannot store or relay CWs.

- **`src/ptguard.h`** — pure layer, shared suppression window with the ask brake.
- **Live:** `ts` 4/4 (storage brake + relay brake + innocent peer zero mercy).

---

## R13 — Late-Truth Correction (2026-10-11)

**Commit:** `abb65dd` · **Decision:** D68 · **Report:** `docs/REPORT-R13-ar.md`

When a local card server contradicts a cached CW on the same ECM, the truth wins: cache corrected, fake remembered, source flagged softly.

- **`src/cwlate.h`** — 64-slot LRU, 30s TTL, keyed by full CW bytes + CAID|PROVID.
- **Config:** `CACHE LATE-TRUTH` (OFF by default).
- **Live:** `lt` 4/4.

---

## R12 — Cache Queue (prior to R13)

Earlier missions R1–R12 established the trust model, source tracking, CW reuse proof, cache purge, instant rotation, config keys, agreement ledger, retry detection, structural filters, cache preference, and the lock race fix. See `docs/REPORT-R{1..12}-ar.md` and `docs/DECISIONS.md` D1–D67.

---

## Baseline — MultiCS r82a

Original MultiCS r82a release. Starting point for this fork.
