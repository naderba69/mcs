# ORIGIN.md — TASK 1.1: CW origin audit and the unified source model

Every line reference in this file was read out of `mcs/src` on the day it was
written and re-checked by a verifier over all documentation. Where a claim could
not be established it says so.

---

## 1. The source types that actually exist

Six, in `ecmdata.h:34-41`. There is **no** source type 5 — the gap is real, not
an omission in this document.

| Value | Name | Meaning |
|---|---|---|
| 0 | `DCW_SOURCE_NONE` | nothing delivered, or attribution cleared |
| 1 | `DCW_SOURCE_CACHE` | a cached CW; the real producer is in the `srcid` flags (§2) |
| 2 | `DCW_SOURCE_SERVER` | an upstream cardserver answered the ECM |
| 3 | `DCW_SOURCE_CSCLIENT` | a Newcamd client answered the ECM it was sent (`#ifdef SRV_CSCACHE`) |
| 4 | `DCW_SOURCE_MGCLIENT` | an mgcamd client answered the ECM (`#ifdef SRV_CSCACHE`) |
| 6 | `DCW_SOURCE_CCCLIENT` | a CCcam client |

Note that 3, 4 and 6 are conditional on `SRV_CSCACHE`, which the build defines,
so all six are live in the shipped binary.

## 2. The origin flags ride inside `srcid`

A cached CW's producer is encoded in bits 16–20 of the same word that carries
the peer id. The values have always lived in `clustredcache.c`; TASK 1.1 moved
them to `src/peer_origin.h` so there is one definition and it can be unit-tested.

| Flag | Value | Producer | Set at |
|---|---|---|---|
| `PEER_CSP` | `0x010000` | CSP-protocol cache peer | `peer_origin.h:28` |
| `PEER_CCCAM_CLIENT` | `0x020000` | CCcam client, cache-exchange | `srv-cccam.c:976` |
| `PEER_CAMD35_CLIENT` | `0x040000` | camd35 client, cache-exchange | `srv-camd35.c:297` |
| `PEER_CS378X_CLIENT` | `0x080000` | cs378x client, cache-exchange | `srv-cs378x.c:430` |
| `PEER_CACHEEX_SERVER` | `0x100000` | upstream cache-exchange server | `cli-cccam.c:499`, `cli-camd35.c:237`, `cli-cs378x.c:307` |

```
bit  31            20 19 18 17 16 15                    0
     [ unused        ] [  origin flags  ] [   peer id    ]
```

**The flags survive every hop**, which is what makes attribution possible at
all:

- `cache_setdcw()` stores the value verbatim in `cwdata->peerid`
  (`clustredcache.c:1448`, and again on a cycle update at `:1483`).
- The cache→ECM pipe writes it as four raw bytes (`put_cache2ecm`,
  `clustredcache.c:562`) and reads it back the same way (`get_cache2ecm`,
  `:541`). No masking anywhere on the wire.

## 3. Every producer of a CW origin — nine sites

| Site | srctype | srcid |
|---|---|---|
| `cli-newcamd.c:287` | `SERVER` | `srv->id` |
| `cli-cccam.c:283` | `SERVER` | `srv->id` |
| `cli-camd35.c:132` | `SERVER` | `srv->id` |
| `cli-cs378x.c:198` | `SERVER` | `srv->id` |
| `cli-radegast.c:149` | `SERVER` | `srv->id` |
| `clustredcache.c:752` | `CACHE` | `pcache->cwlist[i].peerid` |
| `th-ecm.c:360` | `CACHE` | `peerid`, decoded from the pipe |
| `srv-newcamd.c:829` | `CSCLIENT` | `(cs->id<<16) \| cli->id` |
| `srv-mgcamd.c:796` | `MGCLIENT` | `cli->id` |

All nine funnel through one function, `ecm_setdcw(ECM_DATA*, uint8_t dcw[16],
int srctype, int srcid)`. That is the single hook TASK 1.2 and TASK 1.4 need.

**`ecm_setdcw` has two definitions** and only one is compiled:
`setdcw.c:136` under `#ifndef THREAD_DCW`, and `setdcw.c:789` under the implied
else. The build defines `THREAD_DCW`, so the live one is `:753`, which serialises
to a pipe consumed by `setdcw_thread`. Instrumentation must go in the consumer, `ecm_setdcwdata()`
(`:466`), not in `:136`.

## 4. The id namespaces do not collide

Three separate counters, verified in `cfg_set_id_counters` (`config.c:6926`):

- servers and cache-exchange servers share `cfg.serverid`
  (`config.c:6777` and `:6786`), so a server id is unique **across all five
  protocols**. That is why collapsing them into one `DCW_SOURCE_SERVER` is safe:
  `getsrvbyid(srcid)` recovers the protocol from `srv->type`.
- peers use their own `cfg.cache.peerid` (`config.c:262`, plus `clustredcache.c:1792` and `:1832` for peers learned at runtime).
- profiles use `cfg.cardserverid`, clients `cfg.clientid`.

Peer ids and server ids **do** overlap numerically — both start at 1
(`config.c:221`) — but `srctype` distinguishes them, so
`(srctype, srcid)` is unambiguous. **GR4's trust key
`(source_type, source_id, CAID, PROVID, SID)` is therefore well-defined without
any new identifier scheme.**

## 5. `src2string()` is the canonical resolver — build on it, do not reinvent

`main.c:877-979`. This was the most useful thing the audit found, and it is
better than expected:

- handles all six source types;
- handles all five cache origin flags, each with the **correct** lookup and the
  **correct** `& 0xFFFF` mask: `getpeerbyid`, `getcecccamclientbyid`,
  `getcamd35clientbyid`, `getcs378xclientbyid`, `getcesrvbyid`;
- decodes the composite `(csid<<16)|cliid` for `DCW_SOURCE_CSCLIENT` via
  `getcsbyid(srcid>>16)` then `getnewcamdclientbyid(srcid&0xffff)`;
- falls through to `Unknown Source (%d/%d)` rather than guessing.

It is called from eight display sites in `httpserver.c` and from
`setdcw.c:232` and `:548`. **TASK 1.4's trust engine should key off the same
`(srctype, srcid)` pair this function resolves**, so that what the dashboard
names and what the trust engine scores are guaranteed to be the same entity.

Two cosmetic defects in it, not fixed, recorded so they are not mistaken for
attribution bugs later:

- `main.c:2231` — the comment says `srcid = (csid<<16)|cliid` but the code calls
  `getmgcamdclientbyid(srcid)` with the raw value, and the producer at
  `srv-mgcamd.c:796` really does pass a bare `cli->id`. **The comment is wrong,
  the code is right.**
- `main.c:2230` — `DCW_SOURCE_MGCLIENT` returns `ss3`, which reads
  `"newcamd client"`.

## 6. Per-client origin attribution is complete

Every one of the six server protocols stores the origin on its client struct,
on success and on failure:

| Protocol | success | failure |
|---|---|---|
| Newcamd | `srv-newcamd.c:881` | `:841` |
| CCcam | `srv-cccam.c:693` | `:714` |
| camd35 | `srv-camd35.c:125` | `:144` |
| cs378x | `srv-cs378x.c:266` | `:288` |
| freecccam | `srv-freecccam.c:361` | `:378` |
| mgcamd | `srv-mgcamd.c:865` | `:881` |

`radegast` additionally keeps a `lastdcw*` pair (`srv-radegast.c:39-40`,
`:56-57`), which is the pair the display bug in §7 reads.

This matters because it means **TASK 1.2 does not need new per-client
plumbing** — the field that says where this client's last CW came from already
exists and is already maintained on all six protocols.

## 7. The one real defect the audit found, and its fix

**`httpserver.c:3707`** resolved a cache origin like this:

```c
else if (rdgdcli->ecm.lastdcwsrctype==DCW_SOURCE_CACHE) {
    struct cachepeer_data *peer = getpeerbyid(rdgdcli->ecm.lastdcwsrcid);
```

Three of the four other `getpeerbyid()` call sites masked the flags first
(`main.c:773`, `setdcw.c:243`, `setdcw.c:543`, all `& 0xFFFF`). This one did
not, and `getpeerbyid()` compared `peer->id == id` against a value with
`PEER_CSP` still set. **The lookup could never match**, so the radegast
"last used share" column silently never showed the cache peer origin.

**Fix.** Rather than patch the call site, the mask moved inside
`getpeerbyid()` (`clustredcache.c:133`, via `peer_origin_id()`). Callers that
already masked are unaffected; the broken one now works; and TASK 1.4 cannot
repeat the mistake because there is no unmasked path left.

**Risk.** None. Peer ids are assigned from `cfg.cache.peerid`, a small counter,
so no legitimate id has bits above 15 set — and if that ever changes,
`test_peer_origin.c` fails on `max id round-trips through the mask`.

## 8. What the audit does **not** establish

Stated plainly rather than left implied:

- **Whether a given CW's origin is correct.** This audit maps where origins come
  from and proves the encoding round-trips. It does not prove that no path
  assigns a *wrong* origin. That is what TASK 3.5's simulators are for.
- **Whether client-pushed CWs are ever re-pushed onward.** TASK 1.0b closed the
  peer→peer relay (`clustredcache.c:1640`). The client-pushed paths were not
  re-audited here.
- **Anything about CW validity.** Origin and validity are independent; a
  correctly attributed CW can still be wrong. That distinction is the whole
  basis of GR1.
