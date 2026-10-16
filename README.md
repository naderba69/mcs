# MultiCS r82a — Hardened Cache Exchange Server

A hardened fork of MultiCS r82a (x86_64) focused on **cache exchange integrity**, **protocol compatibility with modern peers (oscam r107/cacheex)**, and **observability**. Built for production card-sharing environments where fake CWs, pollution, and blind spots kill viewer experience.

## What This Fork Delivers

- **Late-truth correction** (R13): When a local card server contradicts a cached CW on the same ECM, the truth wins — the cache is corrected, the fake is remembered, and the source is flagged softly.
- **Pollution guard** (R14): Untrusted peers cannot store or relay their CWs until they prove themselves.
- **Channel health monitoring** (R15): Every channel `(caid, provid, sid)` has delivery/refusal/hold counters; a dying channel says its name in the logs before a viewer complains.
- **CAID family tables** (R16): Configurable CA families (`HALFNULLED`, `NOPROVCHK`) replace hardcoded numbers in filters.
- **oscam r107 compatibility** (R17): Wire-format compatibility with oscam cacheex (node lists, rc filtering, push filters, loop detection).
- **MAXHOP_LG** (R18): Multi-hop cacheex relay (up to 10 nodes, wire ceiling).
- **cccam client platform** (R19): Live-tested `getcsbycaidprov` path via real cccam ECM requests.
- **CacheEx visibility** (R20): Per-peer counters (push/relay/drop/filter/loop) on `/cwlog`, `/json`, and telnet — no config key needed.
- **Unified modern-CA layer** (R26–R27): all modern CA systems handled in one format layer (`CA MODERN`), plus SHA-256 / BISS / BISS2 with the lost-answer race fixed.
- **Fake-CW kill gate** (R28): `DCWFILTER PATTERN` refuses shaped manufactured keys at delivery time.
- **Independent-source consensus** (R29): `CACHE CONSENSUS SOURCES: 3` — nine lines echoing one fake never count as agreement.
- **Receiver attestation** (R30–R31): `CACHE ATTEST: 2` — distinct clients re-asking too soon after the same delivered bytes file those bytes (300 s+), the stored entry is purged, and the shared ECM is retired. All client protocols attest; `CACHE PROTECTION: STANDARD` arms the whole stack in one line.
- **Honest shipped profiles** (R32): `multics-r24-optimized.cfg` — 71 profiles, every line verified against the real parser, guarded live by `tests/prof`.

Start from `multics-r24-optimized.cfg` (profiles + shield) and `docs/FIELD-GUIDE-ar.md` (the field guide). Full history: `CHANGELOG.md`; per-round Arabic reports: `docs/REPORT-R*-ar.md`.

All features are **opt-in per peer** (default OFF = stock r82a behavior) to preserve backward compatibility.

## Quick Start

### Build

```bash
cd make-x64
make release              # stock binary
make release-stats        # stats binary (with /cwlog page) — also published under the FIXED delivery name bin/multics.x64
make release-queue        # queue binary
make release-queue-stats  # queue+stats binary
```

Binaries land in `bin/`:
- `multics-r82a-stock-x64` — stock behavior
- `multics-r82a-stats-x64` — with DCW STATS page
- `multics-r82a-queue-x64` — with queue support
- `multics-r82a-queue-stats-x64` — queue + stats

### Run

```bash
./start-multics.sh
```

Or manually:

```bash
bin/multics-r82a-stats-x64 -C multics-ready.cfg
```

### Configure

Edit `multics-ready.cfg` (production-ready) or `multics-sample.cfg` (annotated template).

Key sections:
- `HTTP PORT` / `TELNET PORT` — management interfaces
- `CACHE PORT` / `CACHE PEER` — cache exchange network
- `[ profile ]` blocks — card profiles with CAID, PROVID, USER
- `CACHEEX MAXHOP_LG` — multi-hop relay (R18)
- Per-peer flags: `cacheex_cwcfwd`, `cacheex_rcfilter`, `cacheex_pushfilter`, `cacheex_nodeloop` (R17)

## Testing

```bash
# Unit tests (1643 checks, 42 blocks)
make -C make-x64 test

# Live integration tests (530 scenarios)
make -C tests all

# Soak test (1200s stress test)
cd tests && SOAK_SECONDS=1200 bash soak.sh

# Memory checks (ASan, TSan)
cd make-x64 && bash memcheck.sh all
```

## Documentation

- **[docs/INDEX-ar.md](docs/INDEX-ar.md)** — Document index (Arabic)
- **[docs/STATUS.md](docs/STATUS.md)** — Current status
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — Internal architecture (1710 lines)
- **[docs/FIELD-GUIDE-ar.md](docs/FIELD-GUIDE-ar.md)** — Operator's field guide
- **[docs/COMPAT-ar.md](docs/COMPAT-ar.md)** — Peer compatibility matrix
- **[docs/DECISIONS.md](docs/DECISIONS.md)** — Decision log D1–D76
- **[docs/DEPLOYMENT-ar.md](docs/DEPLOYMENT-ar.md)** — Deployment guide
- **[docs/CONFIG-REFERENCE-ar.md](docs/CONFIG-REFERENCE-ar.md)** — Configuration reference
- **[docs/API-REFERENCE-ar.md](docs/API-REFERENCE-ar.md)** — HTTP/telnet API reference
- **[docs/TROUBLESHOOTING-ar.md](docs/TROUBLESHOOTING-ar.md)** — Troubleshooting guide
- **[docs/TESTING-ar.md](docs/TESTING-ar.md)** — Testing guide
- **[docs/REPORT-*-ar.md](docs/)** — 50+ mission reports (R1–R21)

## Project History

This fork started from MultiCS r82a and underwent systematic hardening across 21 missions (R1–R21):

- **Phase 1 (R1–R12)**: Trust model, source tracking, CW reuse proof, cache purge, instant rotation, config keys, agreement ledger, retry detection, structural filters, cache preference, lock race fix.
- **Phase 2 (2.1–2.10)**: Bounded buffers, login throttling, peer reputation, CW consensus, entropy checks, plausibility filters, cycle detection, negative marking, CM guard, time windows.
- **Phase 3 (3.1–3.19)**: Config parser hardening, XML escaping, field bounds, quote parsing, ECM data structures, DCW filters, stats windows, cache queues, threshold tuning.
- **Phase 4 (4.1–4.5)**: Memcheck, soak, code review, documentation, freeze (D52).
- **Reopening (3.8b)**: `parse_quotes` bounded (D53).
- **Phase R13–R20**: Late-truth, pollution guard, channel health, CAID families, oscam compat, MAXHOP_LG, cccam client, cacheex visibility.
- **Phase R21**: Closure (D76) — soak 1200s PASS, memcheck clean, DoD closed.

See [CHANGELOG.md](CHANGELOG.md) for details.

## Verification

- **Build**: gcc 14.2.0, GNU Make 4.4.1
- **Units**: 1643 checks, 42 blocks, all green
- **Integration**: 530 scenarios, 0 failures
- **Soak**: 1200s stress test PASS (382 ECMs, RSS growth 220 kB)
- **Memcheck**: ASan+UBSan clean, TSan clean (valgrind unavailable in sandbox)
- **Fingerprints**: See [MANIFEST-md5](MANIFEST-md5) (422 entries)

## Ground Rules (GR1–GR10)

All new code respects the project's ground rules:

1. **GR1**: Source attribution before scoring
2. **GR2**: Timeout ≠ failure (separate counters)
3. **GR3**: No punishment on soft evidence; only hard proofs act immediately
4. **GR4**: Detail (type, ID, CAID, PROVID, SID) in every log line
5. **GR5**: MIN-CARD-LATENCY for newcamd replies only
6. **GR6**: Key on `ecm_hash`/`ecmd5`, not SID
7. **GR7**: Probes never reach clients and stay within budget
8. **GR8**: Every harsh action has a reversible log line
9. **GR9**: Hot path has no blocking and no unbounded growth
10. **GR10**: CW from a peer is never re-pushed

## License

MultiCS is open-source. This fork preserves the original license.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Support

- **Issues**: Open a GitHub issue with logs and config (redact credentials).
- **Discussions**: Use GitHub Discussions for questions.

---

**Status**: ✅ Complete — Phase R13–R20 closed (D76, 2026-10-14).
