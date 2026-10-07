# SCOPE.md — what this project will and will not deliver

Written to answer one question honestly: *will the finished product eliminate
black screens, and will it stay compatible with every encryption system?*

Short answer: it will eliminate **one specific class** of black screens, and it
will make the remaining classes **visible and diagnosable** instead of
mysterious. It will not eliminate black screens in general, and no proxy can.
The reasons are below, with evidence.

---

## 1. What black screen actually means here

A black screen has exactly one mechanical cause: **the receiver never got a
control word that decrypts the stream.** Everything else is a route to that.

So the routes are:

| # | Route to "no usable CW" | Fixable by *this* project? |
|---|---|---|
| A | The server delivered a **wrong** CW (fake card, poisoned cache) | **Yes** — this is the master goal |
| B | The server **rejected a valid** CW (over-aggressive filter) | **Yes — TASK 1.10b shipped it.** Both filters are now switchable |
| C | The server **never obtained** a valid CW (no source has the key, subscription expired, card pulled) | **No.** Nothing can create a key that does not exist |
| D | The server delivered a valid CW **too late** (timeouts, latency) | Partially — better routing helps, physics does not |
| E | Client-side fault (tuner, firmware, wrong CAID/PROVID config, signal) | **No** — outside the server entirely |

The 4-phase plan targets **A**, and improves **D** via trust-ranked routing and
zap preference. That is the right target — A is the only route where the server
is actively *wrong* rather than merely unlucky — but it is not the whole set.

**Realistic promise:** black screens caused by fake/poisoned CWs drop to near
zero, and a client recovers within one crypto period when its source turns out
to be fake (TASK 1.6 + 3.5's MTTR SLO). Black screens caused by C, D-partial
and E remain — but after Phase 4 they are **attributable**: the dashboard says
which source was blamed, which route failed, and whether the cache or the
Newcamd path was involved. Turning an unexplained black screen into a named
cause is most of the operational value, and it is achievable.

---

## 2. Coverage gap inside our own findings — this needs a decision

`docs/BLACKSCREEN.md` documents **five** verified root causes and
`docs/DISCUSSION.md` adds a **sixth**. The 4-phase plan covers
**one and a half** of the six.

| Cause | Evidence | Status |
|---|---|---|
| 1. `isbadDCW()` rejects valid CWs (3 equal bytes) | `dcw.c`; proven by test | **FIXED — TASK 1.10b**, `DCWFILTER REPEAT: OFF` |
| 2. `checksumDCW` mandatory, non-standard-checksum bouquets die | `dcw.c`; 18 `acceptDCW` call sites | **FIXED — TASK 1.10b**, `DCWFILTER CHECKSUM: OFF` |
| 3. `SID_FILTER` poisons a card permanently at −100 | `main.c:508-518`, `cli-cccam.c:253` | **Partially** — TASK 1.4/1.5 overlap but the freeze itself is untouched |
| 4. `BAD-DCW` list parsed but dead | `config.c:1762`, check commented out in `dcw.c` | **No** — still open |
| 5. `checkcycle()` disabled entirely | definition `clustredcache.c:541-557` and call `:751`, each inside its own comment block | **Deferred — TASK 1.11b**, see D9: reviving it blind risks the same black screen TASK 1.10b removes |
| 6. `CACHE THRESHOLD` uses `!=` not `<` | `clustredcache.c:1609` | **FIXED — TASK 1.11a**, now a real floor. (`DCW_SKIP` was never the bug: it *is* tested, at six sites) |

Causes 1 and 2 were done first, ahead of the trust engine, because a filter that
silently discards valid keys produces exactly the evidence the trust engine is
designed to act on: clients retry, the source gets blamed, and a good card is
condemned for the server's own filter. Attribution integrity had to come before
attribution logic. That is TASK 1.10 in `CHANGELOG.md`.

**This matters.** For the deployment you described — proxy + cache, CAID 1884
and NLS, "wrong codes on all bouquets" — causes 3 and 6 are at least as likely
as fake cards. Shipping the 4 phases alone would leave a real share of the
black screens standing, and we would not be able to say why.

**Proposal: add TASK 1.10 — per-profile filter policy**, covering causes 1, 2
and 4 behind config switches that default to today's behaviour:

```
DCW FILTER REPEAT:  ON|OFF    # cause 1  (isbadDCW)
DCW FILTER CHECKSUM: ON|OFF   # cause 2  (per profile, NOT global)
BAD-DCW: <16 bytes>           # cause 4  (make the documented option work)
```

and **TASK 1.11 — cache threshold and cycle correctness**, covering 5 and 6.
Both are small, both are testable with the harness we already have, and both
are far cheaper to do now than to retrofit after the trust engine is built on
top of a filter that is still rejecting valid keys.

---

## 3. Will it stay compatible with every encryption system?

Honest answer: **compatible with any CA system that uses 16-byte DVB-CSA
control words under the CAID/PROVID model.** That covers everything in
practical use today — Nagra, VideoGuard/NDS, Viaccess, Irdeto, Conax,
CryptoWorks, DRE, Bulcrypt, and any new CAID added by a provider tomorrow.

### Why it is robust for new systems

The detection layer never parses ECM internals. Its signals are:

- *did the client retry within N ms?* — observed on the client connection
- *did two independent sources return byte-identical CWs?* — byte comparison
- *did this CW appear for two different ECMs?* — byte comparison

None of these depend on which CA system produced the ECM. A brand-new
encryption system that still ships 16-byte CWs over Newcamd/CCcam is handled
with **zero code change** — you add a profile with its CAID and it works.
That is the single most important compatibility property, and it is structural
rather than a promise.

GR6 is already satisfied by the existing key design: the cache keys on
`tag/sid/onid/caid/hash/provid` (`clustredcache.c:296`) and carries `chid`
per ECM (`ecmdata.h:53`), so multi-CHID Irdeto services do not collapse into
false positives.

### Where it would break — stated plainly

**(a) A CA system with a key that is not 16 bytes.** The size is hard-coded in
**60 places** (56 of them 16-byte arrays, 4 of them 8-byte halves):

```
$ grep -rho "cw\[[0-9]*\]\|dcw\[[0-9]*\]" *.c *.h | sort | uniq -c
     35 cw[16]
     21 dcw[16]
      4 cw[8]
$ grep -rho "cw\[[0-9]*\]\|dcw\[[0-9]*\]" *.c *.h | wc -l
60
```

There is no `CW_SIZE` constant. Supporting a different key length would be a
core rewrite touching the cache, the wire format and every protocol module —
weeks, not days, and it would break cache-peer compatibility. **Not in scope,
and not worth pre-building on speculation.** DVB-CSA has been 16 bytes for
three decades; betting on that is reasonable.

**(b) CA-family rules hard-coded in the filters.** These are the real
compatibility landmines, and they are in the filter layer, not the trust layer:

```
clustredcache.c:1482   if ( (req->caid>>8)!=9 )      -> half-null only for 09xx
setdcw.c:195           if ((ecm->caid>>8)!=9)        -> same rule
setdcw.c:508           if ((ecm->caid>>8)!=9)        -> same rule
loadbalance.c:16-18    0x1800 / 0x0900 / 0x0b00      -> provider-match bypass
main.c:543-545         0x1800 / 0x0900 / 0x0b00      -> same bypass
```

A new CA system whose keys are legitimately half-nulled, or which needs the
provider-match bypass, would be mishandled until these lists are extended.
**Mitigation, and I recommend it:** replace the literals with a config-driven
CAID-family table so a new system is a config edit, not a patch:

```
CAID FAMILY HALFNULLED: 09, 0E      # families allowed half-null CWs
CAID FAMILY NOPROVCHK:  18, 09, 0B  # families matched without provider id
```

**(c) A CA system that sends a negative acknowledgement.** If a future system
or protocol could tell the server "that CW failed", the entire client-retry
inference layer (TASK 1.2) becomes redundant — we would use the real signal
instead. That is a *better* problem to have. The design degrades gracefully:
the retry detector would simply stop firing.

---

## 4. Structural limits you should know before we go further

**(a) We are on r82a.** The published binary is r107; 25 revisions of source we
do not have. Fixes that went into r83–r107 are not in our base, and I cannot
diff against them. Everything here is "r82a + our work", honestly labelled
`r82-mcs1`.

**(b) No local card reader exists in this codebase at all.**

```
$ grep -rniE "reader|smartcard|/dev/sci|emulator" *.c *.h | grep -viE "readerr|readerror"
config.h:13:  // FLAG_DELETE: by config reader       <- comment, means "config file reader"
config.h:15:  // FLAG_DISCONNECT: by config reader   <- comment, same
config.h:929: uint8_t shareemus;   // Client use our emu
```

Two comments that are about the *config file* reader, and one flag about a
client using an emulator. There is no card-reader code path, no device open, no
ATR handling. So TASK 3.2 (oracle probe against a real local card) can only
ever be a hook, as your brief already anticipates. The strongest verification
available to us is the corrupted-ECM probe (TASK 3.1), which is good but is an
*inference*, not an oracle. I will not describe it as an oracle.

**(c) Cache peers cannot be authenticated by the protocol.** The CSP-style UDP
exchange has no cryptographic identity. TASK 2.6 can restrict to configured
peer addresses, rate-limit and validate structure — that stops casual
injection, not a determined peer that is already configured. Trust scoring is
the real defence, and it is statistical, so it has a detection delay
(MTTD) rather than instant rejection.

**(d) A source that is right 99% of the time is not "fake".** The trust engine
is deliberately asymmetric and slow to condemn (GR3), because a false positive
that disables a good card is worse than the original problem. So there will
always be a residue of intermittent failures that no scoring policy should
eliminate by being harsher.

---

## 5. What "done" should mean, measurably

I propose we hold the project to the SLOs your brief already defines, rather
than to the word "eliminate":

| Metric | Target | Measured by |
|---|---|---|
| MTTD — fake source detected | ≤ 3 ECMs | TASK 3.5 fake-card simulator |
| MTTR — client recovers | ≤ 1 crypto period (~10 s) | TASK 3.5, single-poison scenario |
| False positives on genuine sources | 0 across all harness scenarios | TASK 3.5 false-positive test |
| Residual black screens | attributable to a named cause | Phase 4 `/api/detection` |

That last row is the honest reformulation of "eliminate black screens": we may
not remove every one, but we will remove the mystery from every one.
