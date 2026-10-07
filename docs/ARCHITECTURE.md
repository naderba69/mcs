# TASK 1.0 — Architecture Map (MultiCS r82a)

Status: **analysis only, no code changed.**
Verified against the extracted r82a tree in `src/` (84 files) on
gcc 14.2.0 / GNU Make 4.4.1.
Sandbox note: the target is Ubuntu LTS / GCC 12+. This box is Debian with
GCC 14.2; all code we write stays C89-compatible (`-std=gnu89`), which
compiles unchanged on GCC 12. A GCC 12 run cannot be reproduced here.

---

## 0. Build & test baseline (already working)

```
cd mcs/make-x64
make release         -> ../dist/multics-r82a-stock-x64    (md5 c84811e0...)
make release-stats   -> ../dist/multics-r82a-stats-x64    (md5 9fa0ec68...)
make test            -> 15 suites / 452 assertions, all against the real src/*.h
```

Seven portability defects in the stock tree had to be fixed first; they are
listed in `docs/BUILD-PATCHES.md` (B1-B7).

### `-Wall -Wextra -O2` baseline — measured, not assumed

Building stock r82 with `-Wall -Wextra` yields **114 warnings**:

| Count | Warning | Nature |
|---|---|---|
| 42 | `-Waddress-of-packed-member` | **by design** — `-fpack-struct` is required (network structs are cast onto raw buffers). Must be suppressed file-wide, not "fixed". |
| 38 | `-Wunused-parameter` | noise |
| 20 | `-Wsign-compare` | noise, but a few hide real bugs |
| 6 | `-Wtype-limits` | noise |
| 4 | `-Wmisleading-indentation` | noise |
| **1** | **`-Wvarargs`** | **real defect** — `debug.c:195` `va_start` second parameter is not the last named argument |
| **1** | **`-Wmaybe-uninitialized`** | **real defect** — `httpserver.c:2119` `'peer' may be used uninitialized` |
| **1** | **`-Wdangling-else`** | **real defect** — `srv-newcamd.c:1075` ambiguous `else` |
| **1** | **`-Waddress`** | **real defect** — `httpserver.c:1347` comparison always true, `&version` is never NULL |

Hotspots: `httpserver.c` 22, `clustredcache.c` 12, `config.c` 10, `main.c` 9,
`th-ecm.c` 8.

**Decision needed (Q1):** a fully clean `-Wall -Wextra` across the *whole*
legacy tree is a separate, invasive cleanup. Proposal: hold **new** code to
zero `-Wall -Wextra`, fix the 4 genuine defects as we pass through them, and
suppress `-Waddress-of-packed-member` + `-Wunused-parameter` globally with a
documented rationale. Otherwise Phase 1 drowns in pre-existing noise.

---

## 1. Data flow (verified end to end)

```
CLIENT (newcamd)
   │  TCP, epoll, non-blocking
   ▼
srv-newcamd.c  cs_cli_recvmsg()            <- client ECM enters here
   │            cs_accept_ecm()            srv-newcamd.c:432
   │            search_ecmdata_any()       srv-newcamd.c:448
   │            cs_store_ecmclient()       srv-newcamd.c:305
   │            pipe_send(prg.pipe.ecm)
   ▼
th-ecm.c  ECM thread
   │   check_sendecm()
   │     ├─ cache path:  pipe_cache_request() / pipe_cache_find()   th-ecm.c:66,120
   │     └─ server path: srvtab_arrange()  ->  loadbalance.c:138     th-ecm.c:130
   ▼
CACHE thread (clustredcache.c)                 SERVER threads (cli-*.c)
   UDP, CSP protocol                            TCP newcamd/cccam/camd35/cs378x
   │                                            │
   └──────────────┬─────────────────────────────┘
                  ▼
        ecm_setdcw(ecm, dcw, srctype, srcid)      setdcw.c:136 / :466 / :833
                  │  acceptDCW() filter           dcw.c:57
                  │  ecm->dcwsrctype = srctype    setdcw.c:261-262
                  │  ecm->dcwsrcid   = srcid
                  ▼
        clients_check_sendcw(ecm)                 th-ecm.c:2
                  ▼
        cs_check_sendcw()   srv-newcamd.c:910     <- walks ALL profiles/clients
                  ▼
        cs_senddcw_cli(cli) srv-newcamd.c:811     <- CW leaves to the client
                  │  cli->lastecm.dcwsrctype = ecm->dcwsrctype
                  │  cli->lastecm.dcwsrcid   = ecm->dcwsrcid
                  ▼
               CLIENT
```

---

## 2. Where the ECM hash / cache key is computed

| Item | Location | Notes |
|---|---|---|
| Hash function | `ecmdata.c:64` `hashCode()` | `h = 31*h + buf[i]`, **32-bit**, over `ecm+3 .. ecmlen-3` (skips tag+length) |
| Hash assignment | `ecmdata.c:260` | `new->hash = hashCode(ecm+3, ecmlen-3)` |
| Lookups by hash | `ecmdata.c:261` (`_dcw`), `:308` (`_any`), `:329` (`_byhash`), `:347` (`_byecmd5`) | the three by-hash/any lookups plus the `CACHEEX` one |
| Stronger key (CACHEEX) | `ecmdata.c:347` | `MD5(ecm+offset, ecmlen-offset, new->ecmd5)` — 128-bit, used by `search_ecmdata_byecmd5` `ecmdata.c:347` |
| Cache key fields | `clustredcache.c:303` `struct cache_data` | `tag, sid, onid, caid, hash, provid` |
| Irdeto CHID | `ecmdata.h:53` `uint16_t chid; // for irdeto` | present and populated |

**GR6 is already satisfiable.** The cache key includes `hash` and `provid`,
and `chid` is carried per ECM — so multi-CHID services do not collapse onto a
SID-only key.

**Risk for TASK 1.3 / GR3 (CW reuse proof):** `hashCode()` is a 32-bit
polynomial hash. Collision probability is not negligible at scale. Using it
alone as "different ecm_hash" evidence for a *definitive* hard action risks a
false positive — which GR3 explicitly calls worse than the original problem.
Mitigation proposal: key the reuse detector on `ecmd5` (already computed,
128-bit) and use `hash` only as the bucket index.

---

## 3. Where client ECMs enter

| Protocol | File | Entry |
|---|---|---|
| Newcamd (primary) | `srv-newcamd.c` | `cs_cli_recvmsg()` → `cs_accept_ecm()` at `:432` |
| Newcamd retry path | `srv-newcamd.c:448`, and `:700-715` | `search_ecmdata_any()`, then `ecm->period++`, `dcwstatus = STAT_DCW_WAIT`, `cachestatus = 0`, `checktime = 1` |
| CCcam | `srv-cccam.c` | `cc_cli_recvmsg()` |
| MGCamd | `srv-mgcamd.c` | |
| cs378x / camd35 / radegast | `srv-cs378x.c`, `srv-camd35.c`, `srv-radegast.c` | |
| FreeCCcam | `srv-freecccam.c` | |

**An existing partial retry mechanism is already present** and matters for
TASK 1.2: `srv-newcamd.c:464-700` detects "client re-sent an ECM whose
`dcwstatus == STAT_DCW_FAILED`" and re-dispatches. It keys on the **ECM_DATA
record**, not on the client, and it carries no source attribution. There is
also `ecm->period` (retry counter) and `cs->option.dcw.retry`.
TASK 1.2 must extend, not duplicate, this.

---

## 4. Where CWs are delivered back to the client

| Step | Location |
|---|---|
| Fan-out to all protocols | `th-ecm.c:2` `clients_check_sendcw()` |
| Newcamd walk | `srv-newcamd.c:985` `cs_check_sendcw()` — iterates `cfg.cardserver` → `cs->newcamd.client` |
| Match condition | `srv-newcamd.c:992` — `cli->ecm.busy && cli->ecm.request == ecm && cli->ecm.status == STAT_ECM_SENT` |
| Actual send | `srv-newcamd.c:862` `cs_senddcw_cli()` |
| Source recorded | `srv-newcamd.c:932-933` — `cli->lastecm.dcwsrctype/dcwsrcid = ecm->dcwsrctype/dcwsrcid` |
| Equivalents | `srv-cccam.c:730` `cc_check_sendcw()`, plus mgcamd/cs378x/camd35/freecccam |

**This is the single best hook for TASK 1.2 and TASK 1.6**: every CW that
reaches a client passes through `cs_senddcw_cli()`, and at that point both the
client (`cli`) and the CW origin (`ecm->dcwsrctype/dcwsrcid`) are in scope.

### BUG FOUND in the delivery path (affects TASK 1.2)

`srv-newcamd.c`, inside `cs_senddcw_cli()`:

```c
int enablefreeze;
if ( (cli->lastecm.caid==ecm->caid)&&(cli->lastecm.prov==ecm->provid)&&(cli->lastecm.sid==ecm->sid) ) {
    if ( (cli->lastecm.status=1)&&(cli->lastdcwtime+200<GetTickCount()) ) enablefreeze = 1;
} else cli->zap++;
```

`cli->lastecm.status=1` is an **assignment, not a comparison**. Consequences:

1. The condition is always true (non-zero), so the intended test never runs.
2. `cli->lastecm.status` is clobbered to 1 on the path — it is later read as a
   validity flag.
3. `enablefreeze` is declared at `srv-newcamd.c:904` with **no initializer**
   and is read at `srv-newcamd.c:961` (`if (enablefreeze) cli->freeze++;`).
   It is only ever written on the inner true-branch, so `cli->freeze` is
   incremented from an indeterminate value on every other path.
   (Correction: this is *not* the `-Wmaybe-uninitialized` in the baseline
   count — that one is `httpserver.c:2119` for `peer`. GCC does not flag
   `enablefreeze` at `-O2` here, so it must be fixed by inspection, not by
   chasing a warning.)

This sits directly on the code path Phase 1 must instrument, so it should be
fixed as part of TASK 1.2 rather than left under new logic.

---

## 5. Cache structure — **TASK 1.1 AUDIT ANSWER**

```c
// clustredcache.c:292
struct cw_cache_data {
    struct cw_cache_data *next;
    uint8_t  cw[16];
    uint32_t cwsum;
    uint8_t  status;      // DCW_ERROR/CYCLE/CHECKED/SKIP/SENT (bits)
    cwcycle_t cwcycle;
    uint32_t peerid;      // "fisrt peerid"  <-- ORIGIN
    uint16_t nbpeers;     // how many peers reported this same CW
};

// clustredcache.c:296
struct __attribute__ ((__packed__)) cache_data {
    ... tag, sid, onid, caid, hash, provid, cwcycle, prevcw[16], ecmd5[16] ...
    struct cw_cache_data *cwdata;   // list of candidate CWs for this ECM
    ECM_DATA *ecm;
};
```

**Verdict: origin tracking EXISTS but is incomplete for our purposes.**

| Requirement | Present? | Evidence |
|---|---|---|
| Which peer supplied a cached CW | **Yes** | `cw_cache_data.peerid`, written at `clustredcache.c:1590` via `cache_setdcw(..., peer->id\|PEER_CSP)` (`clustredcache.c:1806`) |
| How many peers agree | **Yes** | `cw_cache_data.nbpeers`, incremented `clustredcache.c:1594` |
| Distinguishing cache-origin from server-origin at delivery | **Yes** | `DCW_SOURCE_CACHE` vs `DCW_SOURCE_SERVER` (`ecmdata.h:35-36`) |
| Origin recorded for **all** suppliers of one CW | **No** | only the **first** peerid is kept; later agreeing peers only bump a counter |
| Per-(CAID,PROVID,SID) trust granularity | **No** | peer stats are per-peer (`csporthit[]` is per-profile hits only) |

So TASK 1.1 is mostly "extend and expose", not "invent". The gap to close is
a bounded per-origin trust record keyed `(source_type, source_id, caid,
provid, sid)`; the cache itself needs no layout change.

### Cache peer identity

`struct cachepeer_data` — `config.h:170`. Has `id`, `host`, `port`, `recvport`,
`ping`, `protocol`, `ismultics`, `fwd`, `csporthit[]`.
Lookup by address: `getpeerbyaddr()` (`clustredcache.c`, exact `ip && recvport`).
Lookup by id: `getpeerbyid()` (used in `httpserver.c:3707`).

---

## 6. Per-client connection state

`struct cs_client_data` — **`config.h:266`** (packed). Relevant fields:

| Field | Use for us |
|---|---|
| `id`, `pid` (profile id), `cs` | client identity + profile |
| `user[64]`, `pass[64]`, `userhash` | **user-controlled — must be HTML-escaped in Phase 4** |
| `handle`, `ip`, `ipoll` | socket |
| `ecm{busy,status,recvtime,request,hash,climsgid}` | the in-flight ECM (`config.h:345-353`) |
| `lastecm{caid,prov,sid,hash,tag,status,dcw[16],dcwsrctype,dcwsrcid,cardid,decodetime}` | **the last delivered CW and its origin** (`config.h:356-369`; `dcw[16]` at `:364`, `dcwsrctype` at `:365`, `dcwsrcid` at `:366`) |
| `ecmnb`, `ecmdenied`, `ecmok`, `ecmoktime` | existing counters |
| `lastecmtime`, `lastdcwtime`, `lastactivity` | timing |
| `freeze` (`:324`), `zap` (`:325`) | **an embryonic version of exactly what TASK 1.2 needs** |
| `cachedcw` (SRV_CSCACHE) | CW pushed by the client |

**Key finding:** `lastecm` already stores *one* delivered CW with its origin.
TASK 1.2 needs a **ring of N** instead of one, on the same struct — a bounded,
fixed-size addition (`dcwlog[N]`), no allocation, GR9-compliant.

`cli->zap` is already incremented on service change (`cs_senddcw_cli`), which
gives TASK 1.7's "first ECM after a channel change" for free.

---

## 7. Unified source model — already partly present

```c
// ecmdata.h:34-41
#define DCW_SOURCE_NONE      0
#define DCW_SOURCE_CACHE     1
#define DCW_SOURCE_SERVER    2
#define DCW_SOURCE_CSCLIENT  3   // CW pushed by a newcamd client
#define DCW_SOURCE_MGCLIENT  4   // CW pushed by an mgcamd client
#define DCW_SOURCE_CCCLIENT  6   // CW pushed by a cccam client
```

Attribution is set in exactly one place per path — `setdcw.c:261-262` and
`setdcw.c:654-630` — from the `srctype/srcid` passed by the caller:

| Caller | srctype | srcid |
|---|---|---|
| `cli-newcamd.c:308` | `DCW_SOURCE_SERVER` | `srv->id` |
| `cli-cccam.c:283` | `DCW_SOURCE_SERVER` | `srv->id` |
| `cli-camd35.c:132` | `DCW_SOURCE_SERVER` | `srv->id` |
| `cli-cs378x.c:198` | `DCW_SOURCE_SERVER` | `srv->id` |
| `cli-radegast.c:149` | `DCW_SOURCE_SERVER` | `srv->id` |
| `clustredcache.c:752` | `DCW_SOURCE_CACHE` | `pcache->cwlist[i].peerid` |
| `th-ecm.c:360` | `DCW_SOURCE_CACHE` | `peerid` |
| `srv-newcamd.c:829` | `DCW_SOURCE_CSCLIENT` | `(cs->id<<16) \| cli->id` |
| `srv-mgcamd.c:796` | `DCW_SOURCE_MGCLIENT` | `cli->id` |

Resolvers already exist: `src2string()` (`httpserver.c:2992` etc.),
`getsrvbyid()`, `getpeerbyid()`.

The same origin fields exist for the other client protocols too — e.g. the
radegast client keeps `ecm.lastdcwsrctype` / `ecm.lastdcwsrctype` at
`config.h:427-428`, and `httpserver.c:3699-3707` already resolves them via
`getsrvbyid()` / `getpeerbyid()`. So attribution is a codebase-wide
convention, not a newcamd-only feature.

**Two caveats that matter for GR1 / GR4:**

1. `DCW_SOURCE_SERVER` does **not** distinguish newcamd from cccam/camd35/
   cs378x/radegast — all five use it with `srv->id`. `server_data.type`
   (`config.h:782`) carries the real protocol. Any trust key must therefore be
   `(DCW_SOURCE_SERVER, srv->id, server_data.type, caid, provid, sid)`, or we
   risk cross-protocol id collisions.
2. `DCW_SOURCE_CSCLIENT`'s srcid is a **composite** `(cs->id<<16)|cli->id` —
   it cannot be fed to `getnewcamdclientbyid()` unchanged.

**Discrepancy with the stated architecture facts:** the brief says CW sources
are *only* cache peers and Newcamd servers. The code has **six** source types,
three of which are *clients that push CWs* (`CSCLIENT`/`MGCLIENT`/`CCCLIENT`).
That is an additional, currently unmonitored poison vector — a client can
inject a CW. **Decision needed (Q2):** scope Phase 1 strictly to
`{cache_peer, newcamd_server}` as instructed, or include client-pushed CWs in
the trust model from the start? Leaving them out leaves a hole; including them
widens Phase 1.

---

## 8. Threading model and locking discipline

Threads are created only via `threads.c:46` (`pthread_create` wrapper);
`main.c:1743-1810` starts: config, dns, srv, recv_msg, date, telnet, cache,
cacheex, newcamd, mgcamd, cccam, freecccam, cs378x, camd35, http.

Mutexes live in `struct program_data` (`config.h:1519`), initialized in
`main.c:1200-1244`:

| Mutex | Protects |
|---|---|
| `prg.lockecm` | ECM data (main request table) |
| `prg.lockcache` | cache tables |
| `prg.lockcacheex` | cacheex |
| `prg.lockcli` | newcamd client list |
| `prg.lockclimg` | mgcamd client list |
| `prg.lockcccli` / `locksrvcc` | cccam clients / server |
| `prg.locksrv` / `locksrvth` | server list / connection thread |
| `prg.lockrdgdcli` / `lockrdgdsrv` | radegast |
| `prg.lockfreecccli` / `locksrvfreecc` | freecccam |
| `prg.lockmain` | check ECM/DCW thread |
| `prg.lockdnsth`, `prg.lockthreaddate` | dns / date |

Observed discipline: the ECM thread takes `prg.lockecm`, and takes
`prg.lockcache` *while holding* `lockecm` (e.g. `setdcw.c:589`, taken at `:557`), so the
order is **lockecm → lockcache**. New trust tables must respect that order or
they will deadlock against it.

### Race condition on the path Phase 1 must instrument

`THREAD_DCW` is defined in `OPTS`, so the active `ecm_setdcw` variant is
`setdcw.c:466`. It does:

```c
setdcw.c:607   pthread_mutex_unlock(&prg.lockecm);
setdcw.c:613   clients_check_sendcw(ecm);     // <-- no lock held
```

`clients_check_sendcw()` → `cs_check_sendcw()` walks `cs->newcamd.client`,
while the Newcamd thread mutates that same list under `prg.lockcli`
(`srv-newcamd.c:119,129,140`). **`lockcli` is not taken on the delivery walk.**
This is a pre-existing race, and TASK 1.2/1.6 add state to exactly those
structs. Proposal: take `prg.lockcli` around the walk in `cs_check_sendcw()`,
keeping the established `lockecm → lockcli` order and never the reverse.
**This is a structurally risky change (threading) — flagged for your
confirmation before touching (Workflow rule 4).**

---

## 9. Config parser (for TASK 1.9)

Parser: `config.c` (6838 lines), helpers in `parser.c` / `parser.h`
(`parse_spaces`, `parse_int`, `parse_boolean`, `parse_name`, `parse_hex`,
`uppercase`). Style is nested `else if (!strcmp(str, "KEY"))` with an explicit
`':'`/`'='` separator check and `debugf(..., " config(%d,%d): ...",
file->nbline, iparser-currentline)` diagnostics.

Existing option groups we will extend:

| Group | Anchor | Existing keys nearby |
|---|---|---|
| `CACHE` | `config.c:~2860-2910` | `THRESHOLD` (`:2867`), `FILTER`, `FILTER TIME`, `DCWCHECK2`, `AUTOADD` |
| `DEFAULT` profile | `config.c:~1790` | `dcw.check`, `server.threshold` (`:1867`) |
| profile `[...]` | `config.c:~3290-3400` | `DCW CHECK/HALFNULLED/SWAP` (`:3297-3313`), `SERVER THRESHOLD` (`:3397`) |
| `HTTP` | `config.c` HTTP block | `PORT/TITLE/USER/PASS/FILE/EDITOR/RESTART/AUTOREFRESH` |

**Existing options that overlap the Phase 1 list — checked as instructed:**

| Requested | Existing analogue | Verdict |
|---|---|---|
| `RETRY-WINDOW` | `cs->option.dcw.retry` + `ecm->period` (`srv-newcamd.c:718`), `cs->option.dcw.timeout` | **new** — existing is a retry *count*, not a window |
| `SOFT-FAIL-WINDOW` | none | new |
| `BAD-CW-LIMIT` | `cfg.cache.threshold`, `BAD-DCW`, `DCW CHECK` | **shipped (TASK 1.9)** — `threshold` is peer agreement on one key, `BAD-DCW` is a literal list of words to refuse, `DCW CHECK` gates half-null handling. None asks how many bad keys one source may produce. Global, default **1** (avoid on the first confirmed bad CW = the TASK 1.6 behaviour), `rotation_badcw_limit` in `main.c:1100`, parsed at `config.c:1246` |
| `SERVICE-BLACKLIST-TIME` | none | **shipped (TASK 1.9)** — the age limit on a TASK 1.5 purge mark, so `cache_purge_expire()` can lift it. Global, default **0** (never expires = the TASK 1.5 behaviour), `purge_maxage_ms` in `main.c:1102`, parsed at `config.c:1264` |
| `TRUSTED-CACHE-FIRST` | `cs->option.fallowcache`, `cachesendreq/cachesendrep/cacheresendreq` | **shipped (TASK 1.7)** — those gate *participation*, not *trust*. Global, default OFF, `cachepref_enabled` in `main.c`, parsed in `config.c` next to `RETRY-WINDOW` |
| `STATS-WINDOW` | `cfg.cache.alivetime`, `cfg.cache.filtertime` | **shipped (TASK 1.9)** — those are an entry lifetime and a filter window, not a reporting period. Global, default **0** (off), `phase1_stats_window_ms` in `main.c:1103`, parsed at `config.c:1292`; the tick rides the cache thread's existing 3 s wakeup, so granularity is one wakeup |

Runtime defaults live in `config.c:207+` (`cfg->cache.threshold = 1`,
`cfg->cache.filter = 1`). The `update_*()` family (`config.c:5748+`, e.g. `update_cache_servers()` at
`config.c:5748`) moves whole `newcfg` blocks into place on a reload.

The three TASK 1.9 options are deliberately **globals, not `cfg` fields**,
following TASK 1.2's windows: they are reset inside `init_config()` (`config.c:207`, the three assignments at `:258-260`)
and written by the parser, so nothing has to be added to the copy block and a
config file that never mentions them keeps the default it always had. The
trade-off is that they are process-wide rather than per-profile, which is the
right granularity for all three — "how many accidents do I require", "how long
may a proof stand", "how often do I want a line in my log" are statements about
the operator's installation, not about one bouquet.

---

## 10. HTTP server (Phase 4 pre-audit)

| Item | Location |
|---|---|
| Request parse | `httpserver.c:353`, `:386` (`GET `/`POST`) |
| Basic auth | `httpserver.c:7140-7168` |
| 401 response | `httpserver.c:7315-7318` |
| Routing | long `strcmp(req.path, ...)` chain; no table |
| `HTTP FILE` serving | `httpserver.c:7302-7310` |
| Editor write | `httpserver.c:7041` `fopen(fname, "w")` |
| Embedded JS/HTML | `httpserver.c:98`/`:99` (`HTTP_UPDATE_DIV`, `HTTP_UPDATE_ROW`), plus ~10 duplicated inline `imgrequest` blocks, the first at `:1050` |
| CSS | `httpstyle.c` (3 lines, 3.7 KB) |
| Images | `images.c` (37 KB, already in-memory) |

**Findings so far (partial, full audit is TASK 4.1):**

- **No path traversal via URI** in `HTTP FILE`: `httpserver.c:7304` matches
  `strcmp(req.path, file->url)` against admin-configured entries, so the URI
  never selects a filesystem path. Good.
- **Auth bypass on empty username** — `httpserver.c:7143`:
  `if (!cfg.http.user[0] || !cfg.http.user[0]) auth = 1;`
  The same condition is written twice; `cfg.http.pass` is never tested. A
  config with an empty `HTTP USER` silently disables authentication.
- **Escaping:** `cli->user` and peer `program`/`version` are written into HTML
  with `sprintf`. These are network-supplied. Every Phase 4 output needs an
  escaping helper.
- ~10 near-identical copies of the same JavaScript block — the rebuild should
  replace them with one constant.

---

## 11. GR10 audit — **currently violated**

`clustredcache.c:1640-1660`, on receiving a CW **from a cache peer**:

```c
int status = cache_setdcw(&req, cw, cwcycle, peer->id|PEER_CSP);
if ( !(status&DCW_ERROR) ) {
    if (!peer->fwd) {
        cache_send_fwdreply( &req, cw, cwcycle);
    }
}
```

`cache_send_fwdreply()` (`clustredcache.c:1035-1070`) then sends that CW to
**every** peer with `peer->fwd` set and a matching card:

```c
if ( peer->ping>0 )
if ( peer->fwd )
if ( peer_card_binarysearch(peer, pcache->caid, pcache->provid) ) {
    sendtopeer(peer, buf, 30);
```

So a CW received from peer A is relayed to peers B, C, D… **This is exactly
the poison-amplification path GR10 forbids.** One poisoned peer propagates to
the whole mesh. It is also the reason "the cache is the weakest link" in
practice.

Aggravating factor: `CACHE AUTOADD` (`config.c:3076`, used at
`clustredcache.c:1784,1823`) can add **unknown** peers at runtime, and
`AUTOADD: YES, YES` marks them active immediately. Combined with the relay
above, an attacker needs one accepted peer to poison the mesh. That is
TASK 2.6 territory, but the relay is in Phase 1's blast radius because
TASK 1.5 (purge) is useless while poisoned CWs keep being re-broadcast.

**Proposal:** fix the GR10 relay as **TASK 1.0b**, before TASK 1.1 — it is a
~5-line guard, it is independently testable, and every later phase assumes it.
Flagged for your confirmation because it changes cache protocol behaviour.

---

## 12. File/function list per task

| Task | Files | Functions / anchors | Risk |
|---|---|---|---|
| **1.0b** GR10 relay guard *(new, needs approval)* | `clustredcache.c` | `:1650-1665`, `cache_send_fwdreply():970` | protocol behaviour |
| **1.1** source model + origin | `ecmdata.h`, `clustredcache.c`, new `trust.h/trust.c` | `DCW_SOURCE_*` `ecmdata.h:34`; `cw_cache_data:292`; `cache_setdcw():1423`; `setdcw.c:261,599` | low |
| **1.2** retry detector | `config.h`, `srv-newcamd.c`, `setdcw.c` | `cs_client_data:266` (+ `dcwlog[N]`); `cs_senddcw_cli():862`; retry classification in `cs_accept_ecm():440-700` (gate at `:509`); **fix `enablefreeze` bug** | medium (hot path) |
| **1.3** CW reuse LRU | new `trust.c`, `setdcw.c` | hook in `ecm_setdcw()` after `acceptDCW()`; key on `ecmd5` not `hash` | low |
| **1.4** trust engine | new `trust.c/h`, `loadbalance.c`, `th-ecm.c` | new table; decay hook in `th-date.c`; rank in `srvtab_arrange():138` | medium |
| **1.5** cache purge | `clustredcache.c` | walk `cache_data->cwdata` matching `peerid`; under `prg.lockcache` | medium |
| **1.6** instant rotation | `config.h`, `srv-newcamd.c`, `loadbalance.c` | pin field on `cs_client_data`; honour pin in `srvtab_arrange()` | medium |
| **1.7** trusted-cache-first *(done)* | `cachepref.h` (new), `clustredcache.c`, `main.c`, `config.c` | `cachepref_gate()` above `cache_pipe_recvmsg()`, wrapping the four `pipe_cache2ecm_find_success()` sites in `PIPE_CACHE_FIND`; `trust_lock` over all nine `trust_record()` sites | low — ships off |
| **1.8** structural pre-filters *(done)* | `dcwstruct.h` (new), `main.c`, `setdcw.c` | pure `dcwstruct_scan()` called from **both** delivery points in `setdcw.c` (`:160`, `:499` — only `:499` compiles, `THREAD_DCW`) before `acceptDCW()`; `dcwstruct_note()` in `main.c:245` records one `TRUST_EV_SOFT` under `trust_lock` and writes the per-bit counters | low — evidence only, never rejects |
| **2.1** agreement ledger *(done)* | `ledger.h` (new), `main.c`, `setdcw.c`, `srv-newcamd.c`, `statsline.h` | pure `ledger_offer()`/`ledger_failed()` in the header; delivery hook `ledger_note_delivery()` in `main.c:1389` (called from `setdcw.c:604`, above the `STAT_DCW_SUCCESS` return so a discarded key is still recorded); failure hook `ledger_note_failure()` `main.c:1431` (called from the Newcamd retry path, never for a timeout); `ledger_act()` `main.c:1299` under `trust_lock` -> `TRUST_EV_PROOF` + `rotation_note_bad_limited(...,1)` + `cache_purge_mark()` only for `DCW_SOURCE_CACHE`; log line written after the unlock | low — pure functions, fixed 32-entry table, no allocation, no I/O under the lock |
| **1.9** config *(done)* | `rotation.h`, `cache_purge.h`, `statsline.h` (new), `config.c`, `main.c`, `srv-newcamd.c`, `loadbalance.c`, `clustredcache.c` | `BAD-CW-LIMIT` (default 1) → `rotation_note_bad_limited()`/`rotation_should_avoid_limited()`, one count per (source, channel), "avoided" derived from the count; `SERVICE-BLACKLIST-TIME` (default 0) → `cache_purge_should_withhold_at()` + `cache_purge_expire()`, plus the recovery release via `cache_purge_unmark()` that TASK 1.5 left unwired; `STATS-WINDOW` (default 0) → `phase1_stats_tick()` on the cache thread's existing 3 s wakeup | low — all three defaults reproduce the old behaviour exactly |
| **1.0 lock race** *(needs approval)* | `srv-newcamd.c`, `setdcw.c` | `cs_check_sendcw():985` vs `prg.lockcli`; `setdcw.c:607-613` | **high — threading** |

---

## 13. Decisions I need from you before writing code

**Q1 — `-Wall -Wextra` scope.** Clean for new code only (plus the 4 genuine
defects and a documented suppression of `-Waddress-of-packed-member` /
`-Wunused-parameter`), or a full legacy cleanup first?

**Q2 — client-pushed CWs.** The brief lists two CW sources; the code has six,
including `DCW_SOURCE_CSCLIENT/MGCLIENT/CCCLIENT` (a client can inject a CW).
Scope Phase 1 to the two, or include client-pushed CWs now?

**Q3 — GR10 relay fix (TASK 1.0b).** Approve fixing the cache-peer relay
before TASK 1.1? Without it, TASK 1.5's purge is defeated by re-broadcast.

**Q4 — the `lockcli` race.** Approve adding `prg.lockcli` around
`cs_check_sendcw()`'s client walk? It is a threading-model change (Workflow
rule 4), but TASK 1.2/1.6 add state to those structs, so shipping them on top
of a known race would be worse.

**Q5 — the `enablefreeze` assignment bug** (`srv-newcamd.c`, `status=1` instead
of `==1`). Fix inside TASK 1.2, or as a separate pre-task patch?

**Q6 — reuse-detector key.** Use `ecmd5` (128-bit, already computed) as the
identity for the "same CW, different ECM" proof, with `hash` only as bucket
index? Recommended, because GR3's hard action on a 32-bit hash collision would
be a false positive.

---

## TASK 1.5 recon — the cache's real shape (read from source, 2026-09-23)

Purge-by-origin needs to answer one question: *given a source, which cached CWs
did it supply?* What the source actually provides:

**The cache is per-CAID, bucketed, doubly-linked.**
`getcachetabbycaid(caid)` returns a `struct cache_data **` table of
`MAX_CACHE_INDEX+1` buckets, lazily `malloc`'d per CAID (`clustredcache.c:372-391`).
Each bucket heads a list of `struct cache_data`, which carries `next` *and* `prev`
(`:304-305`) — so unlinking an entry is already expressible.

**Each entry holds a list of candidate CWs, not one.**
`struct cache_data` points at `struct cw_cache_data *cwdata` (`:323`), and
`cw_cache_data` is a singly-linked chain of `{next, cw[16], cwsum, status,
cwcycle, peerid, nbpeers}` (`:292-300`).

**Origin is recorded per CW, not per entry** — `cwdata->peerid` (`:298`), set at
`:1590`, `:1625`, `:1637`, carrying the `PEER_*` origin flags from TASK 1.1. The
comment says `// fisrt peerid` (sic) — it is the *first* peer that reported this
CW, and `nbpeers` counts the rest. **So a purge by origin can only reach the peer
that supplied a CW first.** A poisoned CW that a good peer also happened to report
will be attributed to whichever arrived first. That is a real limit on what
purge-by-origin can prove, and it is the strongest argument for purge being
gated on a proof rather than on a score.

**There is no existing unlink path for cache entries.** The only `previous->next =
x->next` in the file is for peers (`:89`) and SMS lists (`:1102`+). Writing one is
a cache-layout change — reserved by workflow rule 4, so it is presented here
rather than implemented.

**A stale code path to not be misled by:** `pcache->cwlist[]` / `pcache->icwlist`
at `:684-689` reference fields that do not exist on `struct cache_data` (which has
`cwdata`, a list). That block cannot compile, which is consistent with it living
inside the same block comment as the disabled `checkcycle()` call (D9). Anything
written against `cwlist` would be writing against dead code.

**The bucket list is a circular MRU list — read, and it is correct.**
`cache_new()` (`:394-460`) inserts at the head and, on the second entry for a
bucket, cross-links the two nodes and advances `cachetab[index] = new`
(`:417-423`). My first reading of `:417-419` alone called this a bug; it is not —
line `:421` also sets `cachetab[index]->prev = new`, so the pair is a properly
linked circular list and the head does advance. Most-recently-inserted is the
head, which is what a purge sweep wants to start from.

**Consequence for 1.5:** because the list is *circular* and the head is the MRU
entry, a sweep that walks `->next` until it sees the head again is the correct
traversal, and unlinking must fix both `prev` and `next` or the ring breaks. A
sweep written as `for (p = tab[i]; p; p = p->next)` on a circular list never
terminates.

---

## TASK 2.1 — the agreement ledger (delivered 2026-09-24)

Full rationale and the six decisions are in `DECISIONS.md` D20; the change log
entry is in `CHANGELOG.md`. What belongs here is only where the code sits.

**The one-sentence shape.** Two independent sources answering for the same ECM
are compared. A disagreement alone does nothing, counts itself and waits; the
client's own failure completes it, and only then is the source whose key the
client was holding accused.

```
setdcw.c:574   ledger_note_delivery(ecm->ecmd5, ecm->hash, caid, provid, sid,
                                     dcw, srctype, srcid, ticks)
                          |
                          |  main.c:928  (trust_lock)
                          v
        +-----------------------------------------------+
        | ledger.h  32 entries, keyed on the 128-bit     |
        |           ecmd5 only -- never the 32-bit hash  |
        |           (D5: a bucket index pairs unrelated  |
        |           ECMs and would manufacture disputes) |
        +-----------------------------------------------+
             |            |             |            |
        LEDGER_AGREE  LEDGER_DISPUTE  LEDGER_SELF  LEDGER_PROOF
        (counted)     (counted)       (counted)    (acted on)
                                                       |
                       srv-newcamd.c retry path  ---------
                       (RETRY_HARD only, never a timeout)
                                                       v
                                             main.c:884  ledger_act()
                                             trust_record(PROOF)
                                             rotation_note_bad_limited(..,1)
                                             cache_purge_mark()  [cache only]
```

**Where each piece lives.**

| Piece | Location | Note |
|---|---|---|
| Table, verdicts, formatter | `ledger.h` (new, no `.c`) | pure functions: no lock, no allocation, no I/O |
| Action on a proof | `main.c:1299` `ledger_act()` | called with `trust_lock` held; the sentence is written after the unlock |
| Delivery hook | `main.c:1389`, called at `setdcw.c:604` (and `:191` in the dead half of the `THREAD_DCW` split) | above the `STAT_DCW_SUCCESS` early return, so a key that is about to be discarded is still offered — the disagreeing key is exactly the one that gets discarded |
| Failure hook | `main.c:1431` `ledger_note_failure()`, called from the Newcamd retry path | `RETRY_HARD` only; reached only when a CW was delivered, so GR2 is structural |
| The key a client is holding | `cs_senddcw_cli()` (`srv-newcamd.c:862`) writes `lastecm.hash` at `:922`, `lastecm.dcwsrctype/srcid` at `:932`, records `lastecm.dcw` at `:948` and zeroes it at `:973` on the decode-failed branch | upstream never wrote the key — see D20 decision 6 |
| Counters | filled at `main.c:1154-1158` (declared just above them), surfaced by `statsline.h` as `agreement N agree, N dispute, N self, N proof, N unplaced` | the STATS-WINDOW line is where a dispute rate is visible without grepping |

**What it deliberately does not do.** No per-server scoring (GR4 is the key, and
the key is per channel). No action on a same-source contradiction. No action
without a client failure. No second proof for the same ECM (latched). No ledger
at all in a build without `CACHEEX`, because there is no `ecmd5` to key on and
every caller then behaves exactly as it did before.

**The two defects it found**, both recorded in `CHANGELOG.md` because both were
invisible until something actually read the value: `cli->lastecm.dcw` was written
by no protocol anywhere in the tree, and the `agr_*` fields of
`stats_snapshot_fill()` were never assigned. The first made every proof fail to
form in silence; the second printed five uninitialised words that looked exactly
like an idle server.

**Live proof:** `make -C tests agreement` (13 assertions, ports 16000-16004/16010,
two cache peers). Phase 1 — one ECM, two peers, no client failure: the dispute is
recorded and nothing is accused, marked or scored. Phase 2 — three ECMs two
seconds apart: exactly one mismatch line, whose accused source is compared against
the source named by the retry line *in the same log*, because the two peers race
and either may deliver first. The dissenter is named and unaccused; the purge mark
is on the accused and no one else; the client keeps being served.

## TASK 2.2 — entropy and collision forensics (delivered 2026-09-24)

Full rationale and the seven decisions are in `DECISIONS.md` D21; the change log
entry is in `CHANGELOG.md`. What belongs here is only where the code sits.

**The one-sentence shape.** Two properties of the key itself — how few distinct
byte values it uses, and how close it is to another key the same source delivered
for a different ECM — become one soft trust event, computed on the delivery path
with no allocation, no I/O and no second observation.

```
setdcw.c:516  cwentropy_note(&trust_tab, ecm->ecmd5, dcw, srctype, srcid, ticks)
                          |            (both delivery points; :165 in the dead
                          |             half of the setdcw.c THREAD_DCW split)
                          v
        +------------------------------------------------------------+
        | cwentropy_note()   main.c:351   (called with trust_lock)   |
        |   cwen_seen()      cwentropy.h  ring of 32 recent keys     |
        |   cwen_scan()      LOWDIV (<=5 distinct values, NDS       |
        |                    null-half exempt)  |  NEAR / NEAR_OTHER  |
        |                    (<=16 bit flips, same ECM excluded --    |
        |                    that is TASK 2.1's business)             |
        +------------------------------------------------------------+
             |                                   |
      one TRUST_EV_SOFT per key            !!! CW ENTROPY: / CW COLLISION:
      (gr3: never a hard action,            written AFTER the unlock
       masks collapse, BAD-CW-LIMIT
       unchanged)
             |
             v
   statsline.h  keys N impossible, N near (same), N near (other)
   (filled at main.c:770-772 inside stats_snapshot_fill())
```

**Where each piece lives.**

| Piece | Location | Note |
|---|---|---|
| Table, verdicts, formatter | `cwentropy.h` (new, no `.c`) | 32 entries, pure functions, no lock, no allocation, no I/O |
| Ring and its duplicate guard | `main.c:350-351` (`cwentropy_tab`, `cwentropy_rep`) | the second table is the same once-per-window guard TASK 1.8 uses, so one key cannot repeat its own event |
| The verdict | `main.c:405` `cwentropy_note()` | takes `trust_lock`, calls `trust_record()` directly (never `trust_record_lk()`); one event per scored mask; the sentence is written after the unlock |
| Delivery hook | `setdcw.c:532`, called at both delivery points | above the `STAT_DCW_SUCCESS` early return, so a key about to be discarded is still examined |
| Counters | `main.c:368` (`cwentropy_lowdiv`), filled at `main.c:1171-1173` | surfaced as `keys N impossible, N near (same), N near (other)` |
| The key a peer pushed | `cachepeer.c` `CP_ALT_CW` | harness only: the first ECM fixes the identity, a later different ECM is answered from the second key, so one peer can serve both phases without a restart |

**What it deliberately does not do.** No action on `NEAR_OTHER` (proximity to
somebody else's key is not this source's fault — GR1). No scoring of an identical
key for a different ECM, which TASK 1.3 owns. No entropy verdict on an all-zero
NDS half, which is protocol. No hard action at all: the layer is soft by
construction, because a false positive that disables a working card is worse than
the black screen it is meant to prevent (GR3). No collision half at all in a build
without `CACHEEX`, because there is no `ecmd5` to compare (the call site passes 0).

**The gap it states rather than hides.** `LOWDIV` has no live check: the live
target's derived key must have 16 distinct values, otherwise the two verdicts
confound each other and the test would no longer show which one fired. The unit
suite covers it (killing it fails 2 assertions); the live run covers the shared
call, log and trust path through `NEAR` (killing that fails 4 live checks and 4
unit assertions).

**Live proof:** `make -C tests entropy` (12 checks, ports 16100-16104/16110, one
cache peer, two phases). Phase 1 serves an honest key and requires silence — no
line and `0 near (same)` in the summary — so the negative control is proven before
the positive one. Phase 2 has the same peer serve a key 3 bits away for a
different service, still passing `checksumDCW()`, and requires exactly one
collision line naming the same source, the measured distance, both keys byte for
byte, and `1 near (same)` in the summary, with no entropy line for the same key.
The peer prints the distance it intended and the server prints the distance it
measured, and the target asserts both.

## TASK 2.3 — card-server latency forensics (delivered 2026-09-24)

Full rationale and the seven decisions are in `DECISIONS.md` D22; the change log
entry is in `CHANGELOG.md`. What belongs here is only where the code sits.

**The one-sentence shape.** The round trip of every ECM request to a newcamd card
server was already measured upstream; TASK 2.3 reads that number and turns it into
one of two verdicts — *faster than a card can answer* (scored, because it is a
statement about physics) or *a metronome* (printed, never scored, because it is a
statement about a pattern) — reported once per source per minute.

**Where the call site is, and why it is the only one.** Upstream stamps
`lastecmtime` when the request goes out and computes `lastecmoktime` when the
reply arrives, and prints it as `(NNms)`. Both the stamp and the console number
stay exactly as they were; this task adds the call, in the same block (the call
site is `cli-newcamd.c:298`):

```
cli-newcamd.c  reply path (the (NNms) the server already logs)
     |  lastecmoktime = GetTickCount() - lastecmtime     [upstream]
     |  cwtime_note(&cwt_tab, id, ...)                   [TASK 2.3, guarded
     |                                                    by lastecmtime != 0]
     v
+--------------------------------------------------------------+
| cwtime.h  cwt_seen()                                         |
|   slot per card server (CWT_SLOTS 64, ~4 KB, .bss, no lock:   |
|   written only by the thread that owns that server)          |
|   ring of the last 16 latencies -> CWT_MAX_FLOOR 2000 ms     |
|                                                              |
|   CWT_FAST   lat < floor_ms            -> SCORED             |
|   CWT_DEGEN  full window, spread <= 5  -> logged, NOT scored |
|                                                              |
|   CWT_SCOREMASK == CWT_FAST  (asserted in test_cwtime.c)     |
|   report once per 60 s: per (source, CAID, PROVID, SID) for   |
|   FAST, per source for DEGEN -- counters still increment for  |
|   every suppressed report                                     |
+--------------------------------------------------------------+
     | verdict arrives (CWT_FAST only)
     v
main.c  cwtime_note()   -- takes trust_lock for one trust_record(),
                        -- releases, then writes the line (GR9)
     | event = TRUST_EV_SOFT, keyed (CAID, PROVID, SID) (GR4)
     v
trust_tab  (same ledger every other Phase 2 detector feeds)
```

**Why the hook is not in the cache or the client path.** GR5: a cache hit is
instant by design, and a client-pushed key was never produced by a card at all.
Keeping the single call site in `cli-newcamd.c` makes "card servers only"
structural rather than a rule someone has to remember when the next protocol is
added.

**The counters, and where they surface.** `cwt_fast`, `cwt_flat`,
`cwt_suppressed` live in `main.c` beside the other Phase 2 counters, are filled by
`stats_snapshot_fill()` (`s->cwt_*`, the third block added to that function), and
appear as the tail of the summary line:

```
 !!! PHASE1 STATS: 2s | struct 48 … | timing 8 fast, 1 flat (0 repeats suppressed)
```

The line grew past 320 characters and was being cut mid-field, so the buffer in
`phase1_stats_tick()` is now 512. `statsline.h`'s format is a bounded `snprintf`,
so the only symptom of getting it wrong is a half-written summary — which is how
it was noticed.

**The harness that had to exist first.** A verdict about how long a card takes
cannot be tested without something that *is* a card. `tests/ncserver.c` (new) is a
Newcamd **server**: it answers the handshake with the server's own
`des.c`/`msg-newcamd.c`/`md5.c`, advertises a CAID, and answers each ECM after a
scheduled delay (`NS_DELAYS`, one value per reply, last value repeating), logging
each reply's target and measured delay under `NS_VERBOSE=1`. It is the first
harness in this project that MultiCS accepts as a *decoder*, which also makes
TASK 1.9's routing half — `rotation_should_avoid_limited()` — observable live for
the first time. Three traps are documented at the top of the file: the LOGIN_ACK
must be encrypted with the keymod-derived key with the passwd-derived key swapped
in only afterwards (`srv-newcamd.c:181-190`); the `N:` key must be 14 separate
two-digit tokens because `parse_hex()` reads one whole run; and `DBG_SERVER`
through the HTTP debug hook is a *filter* that silences `DBG_ERROR`.

**The live target** (`tests/stability.mk`, ports 16200/16203/16204/16210) runs
three phases against one server slot, which is why they share one window:

| phase | card server answers in | required | measured level |
|---|---|---|---|
| 1 honest | 55–75 ms, jittered | silence of both kinds, and `0 fast, 0 flat` in the stats line | floor 42 ms |
| 2 metronome | exactly 55 ms | FLAT line, its disclaimer, and **trust still 0** | 55 > 42 so it must not be called fast either |
| 3 instant | 0 ms | one FAST line per service, **trust 0 → 8**, counters consistent | 30–36 ms on this path |

Every ECM asks a different service and the client rotates over eight user slots:
reusing a service inside `RETRY_WINDOW`, or re-logging into a user slot still
being reaped, makes TASK 1.2 re-route away from the card server and disconnect
the client (both were measured). The target ends with 64 of 64 control words
delivered and the server still alive.

## TASK 2.3a — startup validation and the config thread (delivered 2026-09-24)

Full rationale and the four decisions are in `DECISIONS.md` D23; the change log
entry is in `CHANGELOG.md`. It belongs in this document because it is a fact
about the *thread* model, not about a detector.

**The shape of the startup, before and after.** `main()` creates the config
thread and then has no way to know when that thread has finished reading the
file:

```
before                                  after
------                                  -----
main: start_thread_config()  --creates--> main: start_thread_config()  --creates-->
        usleep(100000)                          usleep(100000)
        if (!cfg.cardserver) exit(1)            if (!cfg.cardserver) exit(1)
        ^ race: 100 ms vs 100 ms                 ^ cannot fire in practice any more

cfg thread: init_config()               cfg thread: init_config()
            read_config()                           read_config()
            usleep(100000)  <-- the other               if (!cfg.cardserver) { message; exit(1); }
                                 100 ms                 usleep(100000)
            check_config()                              check_config()
            ^ crash here: get_cache_caids()             ^ now unreachable with no profile
              read cfg->cardserver->card.caid
```

The profile list is produced inside the config thread by `read_config()`, so the
validation moved to that point in that thread — before the sleep, before
`check_config()`, and before anything can dereference the list.

**The crash that made this visible.** `check_config()` calls `get_cache_caids()`
first, and that function read `cfg->cardserver->card.caid` unconditionally while
the loop underneath it tested `cs` on every iteration. With no `[ profile ]`
section that is a read through a null pointer, reached on every startup and every
reload — but only when the config thread won the race, which is why it read as a
one-in-thirty flake. It now treats an empty list as "no caids known".

**Reload.** The same NULL list arrives through `reread_config()` →
`check_config()`, so an editor that truncates the file and saves could kill a
running server. It cannot crash there any more, and the reload branch logs
`!!! CONFIG: reloaded config has no profile section -- the server is serving
nothing until it is fixed` at `DBG_ERROR`. The rest of the reload path with zero
profiles — stale listening sockets, freed profile structures — is **not**
audited; it is a known gap recorded in `DECISIONS.md` D23 and belongs with the
config work in Phase 3's STRICT-MODE.

**How to reproduce the diagnosis if it ever returns.** No `gdb` in this
environment. A `SIGSEGV` handler injected with `LD_PRELOAD` calling
`backtrace_symbols_fd`, plus an interposed `usleep` that is shortened **only in
non-main threads**, makes the config thread win deterministically and turns a
one-in-thirty crash into a five-out-of-five one. Shims are throwaway (`/tmp`),
never part of the tree.

## TASK 2.4 — plausibility scoring (delivered 2026-09-24)

Full rationale and the six decisions are in `DECISIONS.md` D24; the change log
entry is in `CHANGELOG.md`. What belongs here is only where the code sits.

**The one-sentence shape.** `checksumDCW()`'s four group sums (dcw.c:35) stop
being one boolean and become a graded count per source; 3-of-4 means one damaged
byte, and a *pattern* of those — three keys, one window, one group, 20 % of the
source's traffic — is one soft trust event.

```
delivery paths, both of them, ABOVE the acceptDCW() gate:

setdcw.c:187 / :554   cwp_note(srctype, srcid, dcw, caid, provid, sid, ticks)
        (one of the two compiles: THREAD_DCW picks the second)
                                 |
clustredcache.c:1425  cache_setdcw()  -- the CACHE's own gate, which sits
        UPSTREAM of setdcw.c, so a key it rejects never reaches the hooks
        above; rejected keys are offered here instead
                                 |
                                 v
+---------------------------------------------------------------+
| cwplaus.h                                                     |
|   cwp_groups()  how many of the four sums hold (0..4)          |
|   CWP_NONE      4/4  -> said and counted, nothing else         |
|   CWP_ONE_GROUP 3/4  -> SCORABLE, only as a pattern            |
|   CWP_WEAK      <=2  -> counted, never scored                  |
|                                                               |
|   table keyed on (srctype, srcid)  -- 64 slots, ~3 KB, .bss     |
|   window 60 s: win_all, win_one, win_group, win_mixed          |
|   pattern: win_one >= 3 AND win_one*100 >= win_all*20          |
|            AND never two different groups in one window        |
|   report: once per source per window; counters keep counting   |
+---------------------------------------------------------------+
        | pattern arrives (CWP_SCOREMASK = CWP_ONE_GROUP)
        v
main.c  cwp_note()   -- trust_lock for one trust_record(), released,
                     -- then the sentence (GR9), TRUST_EV_SOFT only
```

**Why the cache needs its own call site.** Every evidence hook in this project is
placed above `acceptDCW()` so that a key about to be discarded is still examined.
That holds inside `setdcw.c` and it does **not** hold for keys arriving through
the cache: `cache_setdcw()` gates first and returns −1, so `setdcw.c` never runs.
Measured live: four forged keys from a cache peer, four replies, zero deliveries,
`0 full, 0 one-group`. The call is on the rejecting path only, so a key that
passes is still counted exactly once, where it is delivered. TASK 1.8 and 2.2 keep
the same blind spot, recorded in D24 rather than fixed quietly.

**Surfaces.** `main.c` holds `cwplaus_tab`, the five counters and `cwp_note()`
(defined before `#include "clustredcache.c"` at main.c:1384, which is what makes
the cache's call site possible). `statsline.h` gained the fifth clause of the
summary line:

```
 !!! PHASE1 STATS: 2s | … | timing 8 fast, 1 flat (0 repeats suppressed) | shape 14 full, 5 one-group (0 limited), 1 pattern
```

`full` is in the line on purpose: a line that only ever shows failures cannot be
read as a rate.

**The live target** (`tests/stability.mk`, ports 16380-16385/16390) uses three
peers, one per phase, each on its own port — a cache peer is identified by
address, so a shared port would make a later phase pass on the report window
instead of on the rate:

| phase | peer answers with | required |
|---|---|---|
| 1 honest | a different *valid* key per reply (`CP_FRESH_KEY=1`, never damaged) | four deliveries, no line, `0 one-group`, `0 patterns` |
| 2 forging | a different key per reply, every one damaged on group 4 | two keys silent, the third producing exactly one line with its count and rate; `4 one-group`, `1 pattern`, trust 0 → 1; no forged key delivered; no proof/reuse/rotation |
| 3 restraint | one damaged key in eleven, the rest valid | no new line, no new score, and the ten valid keys still delivered |

The harness knobs are `CP_FRESH_KEY` and `CP_FORGED_FRESH_EVERY` in
`tests/cachepeer.c`; the peer prints how far each key is from the previous one
(57-70 bits in practice) so the target can assert that TASK 2.2's 16-bit collision
detector has nothing to say about the same traffic.

---

## TASK 2.5 — cache key cycle/parity visibility (observe only)

### The class nothing could see

A control word can be perfect and still open nothing. The even/odd pair a card
produces belongs to one crypto period; hand a client the key of the *previous*
period, or the half opposite to the one its ECM asks for, and the receiver
cannot decrypt — a black screen with sixteen healthy bytes behind it. 2.4 sees
four intact group sums, 2.2 sees full entropy, 2.1 sees no disagreement, 1.3 is
blind *by design* (same SID is never a reuse proof), 1.8 attributes only what
the filters already dropped. In a cache-heavy deployment the stale key is the
common fake, not the exotic one: a peer that is one period late sends exactly
this.

### The rule the tree already lives by

An ECM request declares the half it wants; a forwarding CSP peer may declare the
half it sent. Both live rules agree that the expectation is a function of the
ECM's own tag byte against the channel's `cw1cycle`:

```c
if (!ecm->cw1cycle)      -> NO_CYCLE   /* nobody declared: nothing to judge      */
if (ecm->ecm[0]==cw1cycle) -> CW1CYCLE /* the ECM wants the cw1 half             */
else                     -> CW0CYCLE   /* the ECM wants the cw0 half             */
```

(`put_ecm2cache()`, `clustredcache.c:676-677`; the same rule again in
`ecmdata.c:641-650`.) The dead `checkcycle()` (`clustredcache.c:541-557`, call
commented at `:751`) predates the `cwcycle_t` enum, reads a field written
nowhere, and returns the opposite of what its call site expects — D9/1.11b
keeps it dead; this layer re-derives the rule from the live code instead.

### What the layer counts

`src/cwcycle.h` — per source `(srctype, srcid)`, one 60 s window, two
observation points that funnel through `ccy_note()` in `main.c` under one leaf
lock:

| point | thread | counters |
|---|---|---|
| **offer** — a peer's key, inside `cache_setdcw()` after the pending entry is found (`clustredcache.c:1506`) | cache thread | offered, marked, **contra** (marked *and* different from the entry's declared half `pcache->cwcycle`) |
| **hand-off** — a key given to waiting clients, in `ecm_setdcwdata()` after `clients_check_sendcw()` (`setdcw.c:678`) | setdcw thread | handed, **stale** (handed despite contradicting), **unverified** (the ECM declared, nobody declared the key's) |

The marker travels *outside the key* — byte 15 of a CW is key material — so one
byte is appended to each internal pipe message (cache→ECM: 41 of 48 buffer
bytes used; setdcw: 34 of 64), read back by the existing consumers, and
sanitised to the three wire values (`cwcy_observed()`): anything that is not
CW0CYCLE/CW1CYCLE is "nothing declared". `ecm_setdcw_marked()` is the single new
entry point; every other caller keeps `ecm_setdcw()` and passes no marker.

Verdicts are three, never two: **agree**, **contra**, **unjudged**. Silence is
not guilt — most peers send no marker, and the hand-off side reports
unverified as the size of the exposure window, not as an accusation. One
sentence per source per window, on a contradiction only:

```
!!! CW CYCLE: source 1/65537 ch 1884:000000:0303 -- 1 of 4 cache keys this window contradicted the cycle that ECM declares (expected CW0, saw CW1), 0 keys carried no marker; the last one was offered by the peer -- 0 keys were handed to a waiting client in that state. A key of the previous crypto period, or the other half of the even/odd pair, is genuine and still cannot open the picture. Nothing was rejected, delayed or scored: this is a measurement, not an accusation
```

The STATS-WINDOW line gains: `cycle offered N (M marked, K contra), handed H
(S stale, U unverified, L lines)`.

### What deliberately did not change

The upstream **Check Cycle** gate (`clustredcache.c:1568-1571`) keeps its exact
behaviour: a key whose *declared* marker contradicts the entry's declared half
is marked `DCW_ERROR` and never reaches a client — which is also why, on the
delivery side, *stale* is expected to read 0 in a healthy deployment, and any
other value is a finding. No trust event is emitted (GR3: a declared cycle is
evidence, not proof — the marker can be wrong one forwarder upstream); the live
target asserts trust does not move. The FIND-delivery loops are untouched, so
an undeclared key on a declaring channel is refused exactly as before — now
with a counter that shows how often that happens.

### Tests

- `make test` → `test_cwcycle` (55 checks): the rule, the sanitizer, the judge's
  three verdicts, the one-line-per-window throttle, the two separate
  observation points, LRU eviction (the 2.4 lesson), degenerate inputs, and the
  sentence under a short buffer.
- `make -C tests cyc` (ports 16400-16403/16410): one profile whose SID LIST
  declares `.81` for six channels, one `fwd=1` CSP peer whose marker the
  harness controls (`CP_CYCLE_MARK=1|2` in `tests/cachepeer.c`). Phase 1 honest
  marks: 3 delivered, no line, `offered 3 (3 marked, 0 contra), handed 3 (0
  stale, 0 unverified)`. Phase 2 contradictory marks on fresh keys: refused
  before any client, exactly ONE line naming the halves, trust unchanged,
  `offered 5 (5 marked, 2 contra), handed 3 (0 stale, 1 line)`. Phase 3
  undeclared (29-byte replies): refused, counted as offered-but-unmarked, no
  new line — contradiction and silence stay different things.

---

## TASK 2.6 — negative memory: a proven-bad key is never delivered twice

### The hole

Both definitive proofs convict a KEY — the ledger's LEDGER_PROOF (an
independent source disagreed and the client failed holding the delivered key)
and the reuse proof (the same 16 bytes for two ECMs on two services). Every
consequence was nevertheless shaped around a SOURCE: the 1.5 mark is per
origin, the sweep touches only keys whose `cwdata->peerid` is that origin, and
nothing consults anything at offer time. The same bytes from a relayer, or
from the same source after the entry expired, went straight back to a client.
The proof was real; the memory was not.

### The mechanism

`src/cwneg.h` — a fixed 128-slot table in .bss, keyed on a 64-bit FNV-1a
digest of the key bytes scoped to (CAID, PROVID). Marks arrive only at the
places the two proofs already act (the ledger's PROOF branch — the key the
client held and failed on — and the seven reuse-proof sites: CSP at
`clustredcache.c:1872`, six cache-ex flavours beside their `cache_purge_mark`).
The gate sits in `cache_setdcw()` (defined at `clustredcache.c:1496`, the gate at `:1538`) after the
plausibility note and before the entry lookup: a refused key is not stored,
not counted as peer agreement, and never reaches a client.

```
!!! CW NEGATIVE MARK: ch 1884:000000:00c8 key 11223366445566FF.. proven unable to open its picture on source 1/65537 -- it will not be delivered again, to any client, from any source (GR3 proof memory; no score, no disable)
!!! CW NEGATIVE: ch 1884:000000:012c -- key 11223366445566FF.. was proven unable to open its picture (proven on source 1/65537, 2 s ago) and a cache source offered it again: refused before any client, hit #1 for this key. The refusal is proof-scoped memory, not a score: nothing was scored and no source was disabled
```

The STATS-WINDOW line grows `| negative N live (M proven, H refused, L
line(s))`; the format buffer grew with it (512 → 1024 — the line had begun
truncating mid-segment).

### The scope lesson (recorded in D26)

The first build scoped the memory to (CAID, PROVID, SID) and the live target
failed the right way: the key proven on service 2 was re-offered on service 3
under a different SID and was DELIVERED — a second black screen. The identity
is (key digest, CAID, PROVID): one transponder shares one CW stream across its
services, the SID changes with every zap, and the poison does not. GR6 is
honoured the correct way round — the digest is the primary discriminator, so
two streams sharing a SID cannot suppress each other's live keys unless those
keys are byte-identical to a value that already failed a client.

### What deliberately did not change

No trust event, no rotation, no purge at refusal time: the proof that filed
the key already acted on the origin, and GR3 forbids stacking a second
consequence on the same evidence. The refusal's only guarantee is the absolute
one: these bytes, on this provider, will not be delivered to anyone again.
No TTL — the table is bounded by LRU (marks and refusals both count as
touch), and a full table always evicts, never refuses the next proof.

### Tests

- `make test` → `test_cwneg` (172 checks): digest determinism, fresh vs
  re-proof, the (key, CAID, PROVID) scope with the SID lesson pinned, the
  per-key 60 s line throttle with live counters, LRU eviction under a full
  table, degenerate inputs, and the evidence line (key prefix, not the
  digest) under a short buffer.
- `make -C tests nt` (ports 16500-16503/16510): one CSP peer answers three
  services with the same key. Proof on service 2 (MARK line, exactly once),
  refusal on service 3 (one line, before any client), deliveries pinned at
  exactly the two pre-proof ones, re-proofs stay quiet, stats `1 live (1
  proven, 1 refused, 1 line)`.


## 15. TASK 2.7 — the complement-mirror check (`src/cwcm.h`) — DELIVERED 2026-09-25

The fourth well-formed class. The mirror is a construction, not a corruption:
the second half of the key is the bitwise complement of the first. Verified
against the real `checksumDCW()` (`dcw.c:35`) before anything was written
(`/tmp/mirror_math.c`):

| variant | group sums | outcome |
|---|---|---|
| pure mirror (half2 = ~half1) | groups 3 & 4 off by exactly 2 | refused by the checksum filter, counted weak by 2.4 |
| repaired mirror (fix sums 3 & 4) | 4 of 4 | **passes every layer, DELIVERED silently** |
| one free byte + sums | 4 of 4 | passes, carries 5/6 pairs |

50,000,000 random sum-valid keys: **0** carry >= 4/6 complement pairs.

**The scan** (`cwcm_note()`): six FREE byte pairs only —
(0,8)(1,9)(2,10)(4,12)(5,13)(6,14); the checksum bytes (3,7,11,15) are never
paired, or every well-formed key would read as a mirror. >= 5/6 complements
-> `CWCM_MIRROR`. `cwcm_not_mirror` counts keys that fail the scan.

**The table**: 64-slot LRU keyed (source_type, source_id, CAID) — the habit
is the sender's, SID is evidence-only (D27). A mirror verdict bumps
`cm_mirror_keys`; a non-mirror key bumps `cm_seen`. Full table evicts the
oldest bucket; no allocation, no growth.

**The pattern**: within a 60 s window (`CM_WINDOW_MS`), >= 3 mirror keys AND
>= 20 % of the source's seen keys -> ONE line + ONE `TRUST_EV_SOFT`
(under `trust_lock`, off the ECM path). The verdict itself is throttled to
one per window; `cm_reports` counts lines, `cm_suppressed` counts throttled
verdicts. Single keys: counted, never scored, never printed (GR3).

**The line**, verbatim shape:

```
** CW MIRROR: ch %04x:%06x:%04x from source T/ID built %d of 6 byte pairs as
   bitwise complements (REPAIRED mirror: group sums fixed -- this is why the
   checksum layer passed it). A key built this way cannot open its picture.
   Delivery was not touched by this layer.
```

**Hooks** (three, all evidence-only): `cm_note()` in BOTH `setdcw.c` bodies
(live `:567`, dead `:195` — above the accept gates, reading no filter gate)
and on the `cache_setdcw` reject path (`clustredcache.c`, where a PURE mirror
arrives already refused). Counters: `cm_keys`, `cm_mirror_keys`,
`cm_reports`, `cm_suppressed`; stats segment
`| mirror N keys, M complement-built (K patterns)`.

**Tests**: `test_cwcm` 43/43 (generator builds mirrors by complementing a
sum-valid half 1 — half 1 gets its own sums BEFORE the complement); live
target `cm` (ports 16600-16603/16610): honest keys silent, repaired mirrors
DELIVERED (3 -> 5 -> 6), 2.4 reads them full shape, third key crosses the
pattern -> exactly one line + trust 0 -> 1, no cross-fire with reuse/negative.


## 16. TASK 2.8 — the cache-peer trust engine, coarse tier (`src/trustagg.h`) — DELIVERED 2026-09-25

Two tiers of the same truth. The fine tier (`trust.h`, §"TASK 1.4") scores
(source, CAID, PROVID, SID) and stays the authority for anything that
touches a delivery. The coarse tier sums the SAME events at
(source_type, source_id, CAID) — the sender's habit — with trust.h's exact
arithmetic (SOFT halving, PROOF −25, GOOD +1/32, floor 10, ceiling 100, the
same TRUST_ACTIONABLE line of 25).

| Piece | Where | Notes |
|---|---|---|
| Table | `trustagg_tab`, `main.c` trust block | 128 slots, .bss, LRU evicts oldest (never the worst-scored), key (srctype, srcid, CAID) |
| Taps | beside every `trust_record()`/`trust_record_lk()` | 6 SOFT hooks in main.c + ledger PROOF (via `ledger_act`'s `tagg` buffer) + 8 PROOF/GOOD `_lk` sites (CSP, 6 cache-ex, newcamd ×3) — all under the caller's `trust_lock` discipline |
| The crossing | `trustagg_note()` | below the line + outside the per-entry 60 s window = ONE `!!! PEER TRUST:` sentence + the brake armed (`supp_until = now + 60 s`); re-arms on the next event after the window |
| The brake | `cache_send_request()` fan-out guard | skips the peer's REQUEST share for that CAID while suppressed; `trustagg_gate()` takes trust_lock inside (lockcache -> trust_lock, the cachepref order); pushes/replies never gated; `PEER-TRUST-REQUESTS` default OFF |
| Recovery | the newcamd GOOD tap | keys clients keep climb the coarse score back; one request per window keeps GOOD possible for a skipped peer |
| Stats | `| peertrust N peers, A below the line, R skipped` | peers/below walked under trust_lock on the stats tick; skipped incremented under the same lock |

**The sentence** names the source T/ID, the CAID, the score and the counts
("score %d (%d proof(s), %d soft, %d good)"), what the brake does and for
how long, and ends with what it is NOT and how it ends: "nothing is
disconnected, no client was touched, and keys that clients keep win the
requests back".

**Tests**: `test_trustagg` 48/48 (arithmetic parity with trust.h step for
step, coarse-key independence per CAID, crossing/window/brake, GOOD never
arms the brake, recovery staircase, LRU, sentence, degenerate inputs); live
target `pt` (ports 16620-16624/16630, two peers A/B): honest phase silent,
two reuse proofs on one (peer, CAID) cross the line with exactly one
sentence naming "2 proof(s)", then a seventh service is asked and B's share
is skipped — its request count freezes at three while A answers and the
client still gets its key.


## 17. TASK 2.9 — the cache protocol guard (`src/cacheguard.h`) — DELIVERED 2026-09-25

Below every Phase-2 layer sit the raw datagrams of the CSP exchange, and two
holes lived there, both verified against the tree before any line was
written:

1. **Short-datagram decisions on uninitialised stack memory.** The global
   floor is `received >= 2`; five handlers read past it with no check of
   their own: REQUEST (buf[1..11]), REPLY (buf[12]), PINGREQ (buf[1..3],
   buf[11..12]), PINGRPL (buf[4..5]), HELLO_ACK (buf[4..8]). A short REQUEST
   created a cache entry keyed by a garbage hash; a short PINGREQ anchored a
   peer — under CACHE AUTOADD, created one — at a garbage port.
2. **Invisible senders.** `if (!peer) break;` dropped packets from
   unconfigured senders with no counter and no line.

**The guard** (`cacheguard.h`, pure; hooks in `cache_recvmsg()`):

| Piece | What | Notes |
|---|---|---|
| The gate | `received < cg_minlen(buf[0])` -> drop before any parse | mins = the offsets the handlers read: 12/13/13/6/16/9; unknown types keep the stock floor 2 (no invented policy) |
| Short drops | `cg_short` + one line per 60 s window | "dropped a short TYPE_REPLY (type 2) from ip:port -- 8 bytes, this type needs 13; nothing was parsed ... (N in this window, counted in the stats only)" |
| Unknown senders | `cg_unknown` + one line per window, tap at all 8 `if (!peer) break;` sites | names sender + packet; says stock dropped it before any layer saw it — treatment unchanged |
| Stats | `| cache guard S short, U unconfigured` | counters touched only by the cache thread; the stats snapshot reads the same thread's tick — no lock |
| Byte order | sender printed in `iptoa()` order (LSB first off raw sin_addr) | first live run printed 1.0.0.127; fixed and pinned by a unit check |

**Not needed, verified:** r107's "intolerant cache/ex filter" (reply only if
the cw cycles or is not fake) — our tree already refuses non-`acceptDCW()`
keys at ingest (`cache_setdcw`), strictly stronger than reply-time
filtering. No code; recorded in D29.

**Tests**: `test_cacheguard` 41/41 (the table against what the handlers
read, own-sender datagrams always pass, window semantics including the
tick-0 edge, both sentences with exact evidence, byte-order pin, degenerate
arguments); live target `cg` (ports 16640-16644/16650, honest peer A + dark
pusher S): honest phase silent, S's pushes counted and named with stock's
drop unchanged, A's truncated 8-byte reply dropped before any parse with the
evidence line, A's next intact reply delivers, exactly two lines total.

---

## 18. TASK 2.10 — the trust lifecycle: fade + persistence (`src/trustlife.h`) — DELIVERED 2026-09-25

Until 2.10 the trust picture had two structural holes: a verdict never aged
(a peer condemned once stayed condemned until its LRU slot turned over; a
peer at the ceiling kept its standing forever), and everything lived in
.bss (a restart laundered every proven key, every score, every counters
row). 2.10 closes both.

**The fade (`tl_fade_fine` / `tl_fade_coarse`).** Stateless and computed on
read: for an entry idle `idle = now - lastseen`, points =
`(idle - grace) / step`, and the score moves toward TRUST_START one point
per step, from either side — a floor heals up, a ceiling decays down.
Counters never fade; a fresh event re-shields the entry by moving lastseen;
zero grace or step is a no-op; the same tick recomputes to the same value.
The tick that looks is the cache thread's existing 3-second wakeup, beside
`phase1_stats_tick()`; the walk runs under `trust_lock` (the same leaf lock
the 2.8 gate holds) — off the ECM path. Default grace 1800 s, step 30 s;
`TRUST-FADE-GRACE: 0` disables. Negative memory is not a tier and does not
fade (D26/D30).

**The snapshot (`tl_save` / `lifecycle_load`).** Plain text, v1:
`TF srctype srcid caid provid sid score nproof nsoft age` /
`TA srctype srcid caid score nproof nsoft ngood age` /
`NG caid provid digest key8hex age`. The write runs on the same 3-second
tick every TRUST-PERSIST-EVERY (default 300 s) into `<file>.tmp` + rename —
atomic, bounded by the tables' own capacities, and only when
TRUST-PERSIST: ON (default OFF). The read runs exactly once, in the config
thread, right after the first config parse (so TRUST-PERSIST-FILE is known)
and before any server thread exists (so no verdict can be acted on
half-restored); a SIGHUP reread never re-loads. Ages are re-based onto this
boot's tick counter and pinned at 0 when the file is older than the boot —
an old entry behaves like a very idle one, never like a fresh one. The
parser is strict (kinds, field counts, score within [FLOOR,CEILING], hex
shapes, age > 0 and <= 7 days); a bad line is counted as unusable and
skipped whole. `cwneg_restore` refiles the exact (digest, key8, CAID,
PROVID), touch-not-duplicate; `trustagg_restore` comes back with the fan-out
brake OFF (the request test re-arms it).

**Config**: `TRUST-PERSIST` (off), `TRUST-PERSIST-FILE` (multics.trust),
`TRUST-PERSIST-EVERY` (300 s), `TRUST-FADE-GRACE` (1800 s), `TRUST-FADE-STEP`
(30 s). Stats segment: `| lifecycle N fine, N coarse, N negative restored,
N bad | fade N, snapshots N ok, N failed`.

**Tests**: `test_trustlife` 32/32 (fade grace/idle/START-exact/ceiling/
re-shield/idempotence, coarse parity, TF/TA/NG round trips, 17 strict
rejects, restore idempotence + eviction, the age re-base and its
pin-at-0); live target `tl` (ports 16660-16663/16670): standing written and
faded, snapshot carrying TF/TA/NG, restart with a garbage line appended —
loader restores 2 fine / 1 coarse / 1 negative, refuses the garbage line,
and the first fresh offer of the restored poison key is refused at ingest
with no second mark.

---

## 19. TASK 3.1 — the HTTP request surface (`httpserver.c`, one TU) — DELIVERED 2026-09-25

The web interface is the one surface of this server that speaks to
browsers, and its parser trusted the wire in six places (all upstream):

- **The auth bypass (D31).** `if (!cfg.http.user[0] || !cfg.http.user[0])`
  — the user checked twice, the password never: `HTTP PASS` without
  `HTTP USER` served everything to everyone. Now open only when neither is
  configured; half-configured requires Basic (credential `:<pass>` there).
- **`buf2str` unbounded** — request line and header fields now bounded by
  the destination (`sizeof` at each of the three call sites).
- **`hdrcount` unbounded** — header lines past 20 no longer write past
  `headers[20]`.
- **`explode_post` unbounded** — POST pairs stop at 19, like `explode_get`.
- **`strncpy(...,255)` without terminators** — GET/POST fields get their
  explicit `= 0`.
- **`base64_pdecode` into `pass[256]` unbounded** — payloads over 340 chars
  are refused without decoding (64+64 config bytes cannot encode that far).

Everything fits stock semantics: bounds only narrow what the parser
accepts, well-formed clients are unaffected.

**Tests**: live target `ha` (ports 16680/16681) — honest auth pass/401/401;
the five parser probes (400-char Basic payload, 30 header lines, 600-byte
header value, 700-byte request line, 40-pair POST) each refused or
absorbed AND followed by a healthy authenticated 200 with a clean log;
the pass-only file (stock's wide-open case, proven live against the
pre-fix binary answering 200) now answers 401 without credentials.

---

## 20. TASK 3.2 — client names are data, not markup (`src/htmlesc.h` + `httpserver.c`) — DELIVERED 2026-09-25

Upstream's web interface escaped exactly nine XML/ajax branches
(`xmlescape()`) and wrote client names raw everywhere else: 40 HTML
table-row call sites, three client-detail pages, four XML `<name>`
one-liners, the /debug log printer, and the debug-flag labels. CCcam and
freeCCcam peers supply a `realname` remotely — remote-controlled HTML in
the operator's browser.

**The rule (D32): every render path escapes exactly once.** Builders stay
raw; the 40 HTML branches now run the same `xmlescape(cell[i])` loop the
XML branches always ran; direct write sites use the new bounded
`html_esc()` (`src/htmlesc.h`: only & < > " ' rewritten, honest names
byte-for-byte, no allocation, truncates safely). The /debug page prints
its log ring as text. The telnet DEBUG view stays plain text.

**Open (recorded, D32):** `debugf` formats via `vsprintf` into 1024 bytes
and `add_dbgline` `strcpy`s into 512 — a ~900-byte line would overflow
upstream; protocol buffers keep names far below that in practice. Logging
hardening is its own future task.

**Tests**: `test_htmlesc` 22/22; live target `xs` (16690/16691, a
`<script>alert(1)</script>` username): raw markup on no page, escaped form
on the detail and debug pages, login/200/logs unaffected. Bite: the
pre-fix binary rendered raw=1 (detail) and raw=7 (debug), 0 escaped.

---

## 21. TASK 3.3 — the logging ring never overflows (`src/debugcore.h` + `debug.c`) — DELIVERED 2026-09-25

Everything the server logs — upstream's lines and this project's `!!!`
evidence sentences alike — funnels through one ring:
`debugf()` → `vsprintf` into `debugline[1024]` → `add_dbgline()` →
`strcpy` into `dbgline[..][512]` (.bss). Two unbounded copies back to
back; a caller line over ~900 bytes wrote past the slot into its .bss
neighbours. `fdebugf()` had the same `vsprintf` into 4096 stack bytes,
and the caller's FORMAT string was copied unbounded too.

**The fix (D33): clamp at the destination, keep the semantics.**
`dbg_addline()` clamp-copies a line into the slot (fills to the edge,
terminates, nothing past); `debugf`/`fdebugf` format through bounded
`vsnprintf` and copy the format bounded; the ring's append/wrap/`/debug`
walk are upstream's exact semantics, reproduced by `dbg_store()` and
pinned by the unit. Lines that already fit are byte-identical.

**Bite, live:** the refusal line for an overlong login echoed the
attempted name — the `unknown user '%s'` echo now clamps at 63 (the wire
guard's own bound), so the log cannot carry what the wire guard just
refused. The newcamd harness (`ncclient.c`) got the same clamp before
building its login frame — it was `strcpy`ing a 900-char argv name into
`buf[512]`, its own crash, found while wiring the probe.

**Open from this area:** none. (The harness clamp is harness-side, not a
server defect.)

**Tests**: `test_debugcore` 12/12; live target `dl` (16700/16701): a
62-char username (the widest legal — the wire refuses >63) flows through
five ring lines and /debug renders them escaped with the name whole; the
900-char login is refused as unknown user with a clamped echo; server
and page healthy throughout.

## 22. TASK 3.4 — the cache allocation never crashes the server (`clustredcache.c`) — DELIVERED 2026-09-26

**The defect.** The cache's own memory was the last fully unchecked
allocator in the hot path: `getcachetabbycaid()` (node + 1024-slot table
per CAID), `cache_new()` (three `cache_data` branches), `cache_setdcw()`
and PIPE_CACHE_REPLY (`cw_cache_data`) all malloc'd and dereferenced
without a check. One failed allocation was a SIGSEGV on the cache thread —
which is the process. Bitten live with a surgical LD_PRELOAD bite
(`tests/ca-bite.c`, refuses only the 90112-byte table allocation): the
3.3 binary died with exit 139 on the first honest ECM; the fixed binary
survives the same bite.

**The fix (D34).** Fourteen guards (`TASK 3.4` in the source), one per
allocation and one per caller. Failure semantics everywhere are the same
sentence: the caller drops the one event it was holding, and the ECM's own
`cachetimeout` fallback to the card servers takes over — identical to the
existing "entry past its alive time" outcome. `getcachetabbycaid()` links
its node in only after the table exists, so a half-built node can never be
found. One bounded `CACHE: out of memory (caid %04X)` line per failed
table attempt; per-node failures stay silent.

**Scope discipline.** No pool, no freelist, no struct or layout change
(the reserved categories remain untouched by design choice); nothing
blocks, nothing queues (GR9). Recorded narrowing: a peer-agreement report
dropped for memory is counted nowhere — under pressure sources
under-report, never invent.

**Tests:** live target `ca` (ports 16710–16713, 9/9): phase 1 = the bite
active, two client ECMs, survival + bounded lines + HTTP alive with the
table missing; phase 2 = clean restart, real cache peer, real newcamd
client, the pushed CW delivered byte-exact (the guards did not touch the
happy path). Units 26 groups / 1051 ok; full pass 14 rc=0 / 251 ok.

**Open from this area:** none new. The defect head moves to the config
`USER` parse overflow (`user[64]`, observed, unfixed).

## 23. TASK 3.5 — a config USER line cannot corrupt the client record (`config.c`) — DELIVERED 2026-09-26

**The defect.** The newcamd `USER` branch of `read_config()` parsed the
name and the password straight into `cs_client_data.user[64]` /
`.pass[64]` — adjacent fields of a packed struct — while `parse_str()`
only clamps at 255. Name >63 spilled into `pass`; pass >63 wrote over
`userhash`. Both faces end silently: the entry can never log in again.
Bitten pre-fix, live: `unknown user 'u4'` answered to u4's own
configured 70-char password — the hash was destroyed at parse time.

**The fix (D35).** One hunk (search `TASK 3.5`): parse into the scratch
buffer, bound each copy at its own field edge, one house-format warning
per truncated field (config line + column). `userhash` comes from the
stored bounded name, so it always matches what the wire can deliver.
The truncated pair stays a working credential — the wire guard accepts
<=63, so nothing usable was ever lost to the truncation.

**Recorded for their own tasks (verified, not bundled):**
`parse_str()`/`parse_name()` write `str[len]` for len up to 255 — a
one-byte off-by-one into any `char str[255]` caller (~40 sites, cold
path); and the six sibling USER/PASS branches (camd35, cs378x, telnet,
http, freecccam, cccam) feed identically-shaped `[64]` fields.

**Tests:** live target `cu` (16720/16721, 7/7): two warnings; control
pair untouched; both long entries log in via their 63-char truncated
forms; zero `unknown user` lines. Units 26 groups / 1051 ok; full pass
15 rc=0 / 258 ok (27 live targets).

**Open from this area:** the two recorded parse-layer facts above; the
defect head otherwise moves to `CACHE_FLAG_SENDPIPE` never cleared.

## 24. TASK 3.6 — a cache entry's push arm dies with its waiter (`clustredcache.c`, `setdcw.c`, `th-ecm.c`) — DELIVERED 2026-09-26

**The defect.** `CACHE_FLAG_SENDPIPE` arms a cache entry's push path
(arm sites: the FIND/REQUEST handlers in `cache_pipe_recvmsg`; push
gates: `cache_setdcw()`'s forced push, the FIND scans, the goodcw2/3
walks; plus the `TYPE_REQUEST` auto-answer). Nothing ever cleared it.
One FIND on a hash kept the entry armed for its whole alive time
(45 s default): keys pushed after the waiter's death were force-marked
`DCW_SENT` — buried for every future ECM of that hash, because every
scan skips `DCW_SENT` — and peers asking for the hash got silence.
Live bite on the 3.5 binary: second client decode-failed with the key
sitting in the entry, six peer requests, zero answers.

**The fix (D36).** The arm now has the waiter's lifetime, exactly:
`cache_clear_sendpipe()` runs at the two death moments —
`ecm_setdcwdata()` (SUCCESS, any source, setdcw thread, inside the
existing lockecm section) and `ecm_faileddcw()` (FAILED, ecm thread) —
with the death committed before the clear, and clears only when the
entry still points at THIS ecm. The three arm sites refuse a waiter
that is already SUCCESS/FAILED, which closes the death-before-the-pipe
race by ordering (the FAILED commit precedes the clear; arm decisions
read the status under lockcache, serialized against the clear).
WAITCACHE-to-WAIT does not clear — a WAIT waiter is still servable, the
arm IS the delivery path until the death. No timer, per the task
constraint; no struct change; lock order lockecm-to-lockcache only.

**Map correction worth keeping:** clustredcache.c 703-761 (the
`icwlist` self-answer and the SENDPIPE sets at 727/745) is commented-out
dead text; the live arm sites are the three in `cache_pipe_recvmsg`,
and the live `pipe_cache_find` (763+) only serializes to the pipe.
Verified by preprocessing before the fix was written.

**Tests:** live target `sp` (16730-16733): silent peer, dead waiter,
posthumous push — post-fix the key answers 6/6 peer requests and serves
the same ECM from a second client; pre-fix 0/6 and decode-failed.
Units 27 groups / 1051 ok / 0 FAIL; live suite rc=0 / 283 ok / 28
targets (`mcs/docs/all36-run.log`). cachepref green.

**Open from this area:** upstream's FIND_SUCCESS not checking
`dcwstatus` (recorded in D36, unchanged); the defect head moves to the
`parse_str`/`parse_name` `str[255]` off-by-one (D35).

## 25. TASK 3.7 — the parse clamp keeps its terminator inside (`parser.c`) — DELIVERED 2026-09-26

**The defect.** Six token readers (`parse_str`, `parse_name`,
`parse_value`, `parse_int`, `parse_hex`, `parse_bin`) clamped at 255
and wrote `str[len]=0` — a 255+-char token put the terminator one byte
past any caller's `char str[255]`. Real buffers hit: `read_config`'s
scratch, `parse_server_data`'s, `twin_read_chninfo`'s, and
`parse_boolean`'s own local. Usually silent (padding), occasionally
fatal (a neighbouring pointer/int) — the latent kind.

**The proof instrument this task leaves behind.** The real server
compiled under ASan for the three parse-buffer-owning TUs (rule
`x64/multics-asan`; the other objects link uninstrumented), with
`-fsanitize-recover` so one run reports every site. Pre-fix: eight
`WRITE of size 1` reports in one startup. The live `ps` target keeps
this as a permanent guard (zero reports + HTTP 200 + profile parsed +
the plain binary boots the same config).

**The fix (D37).** Clamp 254 in all six functions (search `TASK 3.7`):
the terminator lands at `str[254]`, inside every 255-byte caller,
still NUL-terminated. 254-char tokens unchanged; 255+ truncate one
char earlier. Unit canary `test_parser` (36 asserts, offsets
254/255/256) pins it forever.

**Recorded for their own tasks:** `parse_quotes()`'s unbounded
`strcpy` (needs a quote in the line); the D35 sibling USER/PASS
branches (TASK 3.8). Widening the ASan build to all TUs is 4.1.

**Tests:** units 28 groups / 1087 ok / 0 FAIL; live suite 29 targets
rc=0 / 0 outcome failures (`docs/all37-run.log`). Releases: stock
`a173e2946e7c83f3d0870c1e22b073e7`, stats
`4650e5d9204d5b4a69d5503f4e4ec851` (735,544 bytes each; dev strips
byte-identical).

**Open from this area:** the two recorded items above; the defect head
moves to the sibling USER/PASS branches (3.8).

## §26 — the bounded config-credential copy (cfgfield.h) — TASK 3.8

One helper answers every "put this config token into this char field"
on the read path: `cfg_store_field(dst, dstsize, tok, nbline, col,
what)` in `src/cfgfield.h`. It copies at most `dstsize-1` bytes, always
terminates inside the field, and emits exactly one house-format warning
when truncation happened — naming the config line and column and the
field's role ("USER name too long..."), closing with "the truncated
pair stays a working credential".

**Where it is used.** Every USER/PASS config branch except newcamd
(which got the same recipe by hand in 3.5, kept as-is): camd35 and
cs378x client USER, cccam F-lines (`link_cccam_client`), mgcamd users
(`link_mgcamd_user`), telnet USER/PASS, http USER/PASS, freecccam
USER/PASSWORD. Grep `TASK 3.8` in config.c: 12 markers, 14 calls, 11
live sites. `ucol38` is declared once at the top of read_config to
carry the column.

**Why a header.** The unit test must exercise the exact production
code; a header is includable twice with a stub escape
(`CFGFIELD_STUB_DEBUGF` supplies debugf/getdbgflag from the test).
Production always includes it after dcwfilter.h, where debug.h is
already in scope — including debug.h from cfgfield.h itself would
redeclare `struct trace_data` (bitten once, fixed in the same round).

**The struct contract it protects.** `user[64]` is immediately followed
by `pass[64]` in every packed credential struct; after pass come
userhash (cccam/camd35/cs378x/mgcamd), maxusers (freecccam), or the
owning thread's pid/tid (telnet/http inline config structs). Any
out-of-field write rots the credential, the hash, or a thread handle.
cfg_store_field's canary-proved guarantee: writes stop at the field
edge, byte-stable beyond it. userhash is hashed over the STORED name
after the copy (the 3.5 contract), so wire credentials and hashes can
never disagree.

**Invariants to keep when touching this code.**
- New config fields that accept user-supplied strings go through
  cfg_store_field; never parse_str straight into a fixed field.
- The warning is per-field, once, at parse time; the log never spams on
  the hot path.
- Truncated credentials keep authenticating: post-truncation is a
  documented, warned, working state — not a rejection.

## §27 — the bounded web cell escape (xmlescape) — TASK 3.9

`xmlescape(char *str)` escapes a web table cell IN PLACE: it expands
`& < > " '` into entities in a frame, then copies the expansion back
into the caller's buffer. Its contract, since TASK 3.9:

- The frame `exml[2048]` equals the callers' cell width. Every call
  site passes a `char[N][2048]` row (verified across httpserver.c: 8,
  10, 11 and 12 rows). Escaped text caps at 2047 chars — the walk
  breaks at `exml+2041` so a 6-char entity plus terminator always fit —
  and the copy-back writes at most the caller's full 2048 bytes: never
  one past.
- Truncation warns ONCE in the house format ("xmlescape: cell text too
  long, truncated at the cell edge; the page stays valid") and the
  truncated text is always whole entities — the page stays parseable.
- Honest cells (a few hundred chars) are byte-identical to upstream,
  same entity set including `&apos;`. `html_esc` (3.2, `&#39;`) is a
  DIFFERENT escape used for its own call sites; do not merge the two
  entity sets.
- The profiles page/div tail escapes its 11 cells like every other row
  branch (3.2 completion, landed with 3.9). New page code that renders
  config- or peer-derived strings into rows must pipe its cells through
  `xmlescape` — grep the existing `TASK 3.2`/`TASK 3.9` markers for the
  pattern.
- The regression guard is the ASan probe `x64/probe_xmlescape` (the
  real TU, renamed, called directly) run first by the live `xe` target.

**Recorded boundary:** `/profiles?action=xml` prints `<name>` raw
(operator-owned value, external consumers) — D39. If that branch ever
feeds anything cell-shaped, it must adopt the same escape.

## §28 — the DCW STATS switch (TASK 3.10)

`DCW STATS: ON|OFF` arms `acceptDCW()`'s rejection counters. Contract,
since TASK 3.10:

- It is a sub-word of the existing top-level `DCW` handler in
  `config.c`, the same handler that owns `DCW TIMEOUT`, `DCW MAXFAILED`
  and `DCW RETRY` for the current profile. Do not add a second `DCW`
  branch earlier in that chain: it matches first and the profile lines
  silently keep their defaults (bitten by the `sp` target; pinned by
  `ds`, which requires the profile page to still show the configured
  timeout).
- STATS is global. It is accepted with no profile open, and it is
  accepted again inside a profile. Other sub-words still require a
  profile, with the same "Skip DCW, undefined profile" warning as
  before.
- Default off. `init_config` resets `dcwstats_on` on every parse,
  including the inotify reload (`th-cfg.c` → `reread_config`), so a
  file that drops the line stops the gathering.
- `parse_boolean()` rules: 1/0/ON/OFF/YES/NO, anything else is OFF, and
  the log says which one was read.
- The counters exist only in a `-DMCS_DCWSTATS` build (the stats
  release). A stock build parses the line, logs one warning, and does
  not reference the symbol. The include, the reset and the assignment
  are all under that flag.
- The counters' meaning (checksum, null/half-null, repeat-3-bytes,
  accepted) is `dcwstats.h` and is proven by `build/test_dcwstats`.
  Reading them out is 3.11 (telnet) and 3.12 (web); until those land,
  ON gathers and nothing displays.

## §29 — the telnet DCW-stats readout (TASK 3.11)

`dcwstats` and `dcwstats reset` are the operator's hand on the counters
TASK 3.10 arms. Contract, since TASK 3.11:

- No second word prints a status line and one count line per slot:

      dcwstats: ON
      accepted <n>
      checksum <n>
      null/half-null <n>
      repeat-3-bytes <n>
      bad-dcw-list <n>

  `ON` or `OFF` is `dcwstats_on`. The names are
  `dcwstats_reason_name()` and must be reused verbatim by the web page
  (3.12). Do not invent a second spelling.
- `dcwstats reset` calls `dcwstats_reset()` and answers
  `dcwstats: counters reset`. It zeros even when gathering is OFF, so
  leftovers can be cleared before the switch is turned back on.
- Any other second word answers `dcwstats: usage: dcwstats [reset]`
  and changes nothing.
- A stock build (`MCS_DCWSTATS` unset) does not reference the symbol.
  The command still answers, once:
  `dcwstats: this build has no counters; use the stats release`.
- `help` lists `dcwstats` on both builds.
- No new lock. A torn increment on these counters was already accepted
  in `dcwstats.h`.

## §30 — DCW stats on the home page (TASK 3.12)

The home page section and the telnet command are one readout. Contract,
since TASK 3.12:

- It is rendered by `http_send_index`, inside the block the full page
  and `/?action=div` share. Do not add a second copy on another page.
- Row names are `dcwstats_reason_name()`, in that order, passed through
  `html_esc` before interpolation. Counts are `%lu`. The status word is
  `ON` or `OFF`. A web reset does not exist; `dcwstats reset` is telnet.
- A stock build does not reference `dcwstats_on`. The section says
  `this build has no counters; use the stats release`, the same
  sentence as telnet, also escaped.
- The refresh body is upstream's header-less fragment. That is not a
  3.12 change. A client that requires an HTTP status line will not
  parse it; the page's own script does.

## §31 — the card-score floor (cardsids_update) — TASK 3.13

`cardsids_update` (`main.c:1774`) keeps one integer per
`(card, prov, sid)`. Callers in `cli-*.c` pass +1 or −1. Contract,
since TASK 3.13:

- −100 is a cap. Further failures do not pass it. A success at the
  floor sets 0, the same reset the negative branch already does above
  the floor. The +100 ceiling is not part of this contract and is
  unchanged.
- Who is penalized did not change. A rejected CW still scores −1.
- The consumer is `srvtab_arrange` (`loadbalance.c:138`).
- Its gate reads `maxfailedecm` (`loadbalance.c:223`). Zero, the
  default, leaves the gate open.
- A set `maxfailedecm` (`config.c:3819`, clamped 0–100) skips a card
  at `val <= -maxfailedecm`. A score that returns to 0 can be chosen
  again.
- Two log lines, and only on the transition: `!!! CARDSIDS:` at the
  floor, and again on recovery. They use `DBG_ERROR`, so they follow
  the same debug switch as the other `!!!` lines.

## §32 — the BAD-DCW snapshot (TASK 3.14)

`BAD-DCW` (`config.c:2053`) still parses into `cfg.bad_dcw`. That list
is not what `acceptDCW` walks. Reload frees the nodes, and the ECM
path must not follow a pointer the config thread is about to free.

Contract, since TASK 3.14:

- After the swap, `dcw_badlist_commit` (`config.c:6530`) copies up to
  `DCW_BADLIST_MAX` keys (`dcw.h:19`, 32).
- `dcw_badlist_publish` (`dcw.c:136`) replaces a fixed table.
- Startup calls `dcw_badlist_commit` (`th-cfg.c:51`) once, after
  `read_config`.
- `reread_config` calls `dcw_badlist_commit` (`config.c:6625`) after
  the swap and before the free.
- `acceptDCW` calls `dcw_on_badlist` (`dcw.c:192`) after the three r82a
  tests. A hit returns 0. With `DCW STATS` on, the reason is
  `bad-dcw-list` (`dcwstats.h:83`).
- An empty table is the default. No `BAD-DCW` line logs nothing and
  changes no decision.
- A reader that sees an odd sequence counter does not reject. A false
  reject during a reload is worse than one listed key passing.
- Keys past 32 are not stored. One `!!! BAD-DCW:` line names how many
  were dropped. Dropping the line logs `list cleared` once.
- `dcwfilter.c` is still not compiled. The walk lives in `dcw.c`,
  which is the file OBJECTS already links.
