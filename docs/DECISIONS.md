# DECISIONS.md — decision log

Owner: the engineering lead (Arena agent), delegated by the project owner on
2026-09-22 ("you are the lead engineer and the programmer responsible for
developing and fixing the project").

Rules I still follow despite owning the decisions:
- Workflow rule 4: a change to the **threading model** or the **cache layout**
  is presented and waits for confirmation. Those items are listed under
  "Escalated" and are NOT implemented until approved.
- Everything else I decide, implement, test, and record here with
  what / why / risk / rollback.

---

## D1 — Scope of `-Wall -Wextra -O2` cleanliness

**Decision.** New code must compile with zero `-Wall -Wextra` warnings.
Legacy code is not mass-cleaned in Phase 1. Two warnings are suppressed
globally with this rationale, and the four genuine defects are fixed as we
pass through them.

Suppressed globally:
- `-Waddress-of-packed-member` (42 occurrences) — `-fpack-struct` is
  mandatory: MultiCS casts raw network buffers onto its structs. Taking the
  address of a packed member is the design, not a defect.
- `-Wunused-parameter` (38) — noise across 84 legacy files.

Fixed as genuine defects (measured baseline):
| Location | Warning | Fix |
|---|---|---|
| `debug.c:195` | `-Wvarargs` | `va_start` second argument is not the last named parameter |
| `httpserver.c:2119` | `-Wmaybe-uninitialized` | `peer` may be used uninitialized |
| `httpserver.c:1347` | `-Waddress` | comparison always true (`&version` never NULL) |
| `srv-newcamd.c:1035` | `-Wdangling-else` | add explicit braces |

**Why.** A full legacy cleanup would touch 84 files before Phase 1 delivers a
single user-visible fix, and would bury real findings in noise.

**Risk.** Low. The suppression list is explicit and narrow.

**Rollback.** Remove the two `-Wno-` flags; the 80 warnings return.

---

## D2 — Client-pushed CWs (`DCW_SOURCE_CSCLIENT/MGCLIENT/CCCLIENT`)

**Decision.** Phase 1 targets `{cache_peer, newcamd_server}` as instructed.
Client-pushed CWs are **counted and attributed from day one, but never
penalized in Phase 1**. They become a first-class trust subject in Phase 2.

**Why.** Leaving them unrecorded would blind Phase 2 and would let a client
inject a CW that later gets blamed on an innocent server — a direct GR1
violation. Recording them costs one counter and removes that risk. Penalizing
them now would widen Phase 1 beyond its goal (the client stops being black).

**Risk.** Low. No behaviour change for client-pushed CWs in Phase 1.

**Rollback.** Delete the counters; nothing else depends on them yet.

---

## D3 — GR10 relay: **implemented now, no new option**

**Finding that changed the plan.** `CACHE FORWARD` already exists
(`config.c:3106`, `cfg.cache.forward`, hot-reload at `config.c:6238`), but it
guards only **one** of the two relay paths:

| Path | Location | Guarded by `cfg.cache.forward`? |
|---|---|---|
| A — answer a peer's request from cache | `clustredcache.c:1571` | **Yes** |
| B — re-push a peer's CW to all forwarding peers | `clustredcache.c:1619-1621` | **No** |

So this is not a missing feature; it is a documented switch that does not
control half of what it documents. Path B is the GR10 violation and the
poison-amplification route.

**Decision.** Gate path B on `cfg.cache.forward`, making the existing
documented option govern both paths. No new config key (GR: "check for
existing similar options first"). Default stays `0`, so `CACHE FORWARD: OFF`
— the default — now satisfies GR10 with no configuration.

**Why not a new `CACHE RE-PUSH` option.** Two keys for one concept invites
operators to set them inconsistently, which is precisely how the current bug
survived.

**Risk.** Behaviour change: operators who relied on relay while leaving
`CACHE FORWARD` unset will stop relaying. That is the documented meaning of
the option and is required by GR10. The switch is reversible in the config.

**Rollback.** `CACHE FORWARD: ON` restores the old behaviour immediately,
without recompiling.

---

## D4 — `enablefreeze` defect: fixed as its own task, before TASK 1.2

**Decision.** Fix `srv-newcamd.c:853-895` as a separate, separately-tested
change ahead of TASK 1.2, not folded into it.

**Why.** TASK 1.2 adds state to `cs_senddcw_cli()`. Building new logic on top
of an uninitialized read makes any later bug ambiguous — we could not tell
whether a regression came from the new ring buffer or from the old defect.

**Risk.** Medium. `enablefreeze` is read at `srv-newcamd.c:895`
(`if (enablefreeze) cli->freeze++;`) from an indeterminate value today, so
`cli->freeze` is currently arbitrary. Initializing it to 0 makes the counter
deterministic. `cli->freeze` is reported in the web interface and used by the
`CHECK_NEXTDCW` logic, so the reported number will change — from random to
correct.

**Rollback.** Revert the two-line change.

---

## D5 — Identity for the CW-reuse proof (GR3)

**Decision.** Key the reuse detector on `ecmd5` (128-bit MD5, already computed
at `ecmdata.c:264` when `CACHEEX` is defined, which it is). Use the 32-bit
`hash` only as a bucket index.

**Why.** `hashCode()` (`ecmdata.c:64`) is `h = 31*h + buf[i]` over 32 bits.
GR3 permits an *immediate hard action* on "identical CW for two different
ecm_hashes". Acting on a 32-bit collision would blacklist a genuine source —
and GR3 states a false positive disabling a good card is worse than the
original problem.

**Risk.** Low. Requires `CACHEEX`; it is in `OPTS`. If a build ever drops
`CACHEEX`, the detector must fall back to "disabled", not to the 32-bit hash.
That fallback is mandatory and is asserted by a test.

**Rollback.** Feature-flag the detector off.

---

## D6 — Cache entry layout

**Decision.** No change to `struct cache_data` / `struct cw_cache_data`.
Origin tracking already exists (`cw_cache_data.peerid`, `nbpeers`). Trust
state lives in a **separate** fixed-size table keyed
`(source_type, source_id, caid, provid, sid)`.

**Why.** The cache layout is shared with the wire protocol and is
`__attribute__((__packed__))`; changing it is a structurally risky change
under Workflow rule 4, and it is unnecessary because the data we need is
already there.

**Risk.** None to the cache.

**Rollback.** n/a.

---

## Escalated — NOT implemented, awaiting your approval

### E1 — CORRECTED: there is no client-list race; only a narrower fd race

**My original escalation was wrong, and I am replacing it rather than leaving
both versions standing.**

I claimed that `cs_check_sendcw()` (`srv-newcamd.c:985`) walks the Newcamd
client list without `prg.lockcli` while the Newcamd thread mutates that list,
and asked for approval to take the lock. **The mutation claim is false.**

`cs_disconnect_cli()` (`srv-newcamd.c:34`) does only this:

```c
cli->connection.status = 0;
cli->connection.uptime += ticks - cli->connection.time;
cli->connection.lastseen = ticks;
close(cli->handle);
cli->handle = -1;
```

No linked-list surgery, no `free()`. The only function that unlinks and frees
client nodes is `remove_newcamd_clients()` (`config.c:5795`), and its single
call site is `config.c:6449` — inside the cleanup of the **temporary** `newcfg`
after a hot-reload, never the live `cfg`. The list is therefore structurally
stable at runtime, and the walk cannot follow a dangling `next`. My proposal
would have added lock contention on the hot path to prevent a crash that cannot
happen.

**What is still genuinely wrong**, narrower and worth fixing on its own:

1. **fd reuse.** `close(cli->handle)` runs on the client thread while the ECM
   thread may be about to `cs_message_send()` on the same fd. The number can be
   recycled by an unrelated new connection before the send runs, so a CW can be
   written to the wrong socket. `cli->handle = -1` happens *after* `close()`,
   which widens the window.
2. **`memset` vs read.** `cs_connect_cli()` holds `lockcli` while it does
   `memset(&cli->ecm, 0, sizeof(cli->ecm))`; the ECM thread reads `cli->ecm.*`
   with no lock at all.
3. Plain non-atomic reads and writes of `cli->ecm.busy`, `cli->handle` and
   `cli->ecm.status` across the two threads.

**Status: no longer escalated, no longer blocking.** TASK 1.2's ring buffer will
live in `cs_client_data` nodes that are never freed at runtime, so 1.2 does not
depend on this. The fd-reuse race stays on the list as its own task, to be
verified with TSan rather than by inspection.

Worth noting: the upstream author clearly considered this, because the matching
lock pair is present but commented out at `srv-newcamd.c:1139` and `:1150`.


### E2 — Anything touching `struct cache_data` layout

Not planned. Recorded so that if a later task seems to need it, it comes to you
first.

### D7 — `DCWFILTER` gates are global, not per-profile

The brief asked for per-profile filter policy. The shipped gates
(`DCWFILTER REPEAT|CHECKSUM`) are global, because `acceptDCW()` is called from
the cache and from every server protocol at 18 sites where no profile is in
scope — `clustredcache.c:1405`, `setdcw.c:164` and `:472`, `srv-newcamd.c:768`
and eleven others. A per-profile variant means threading a parameter through all
18 call sites, several of which run before the profile is resolvable.

Global is also the safer default here: a filter that accepts a valid key for one
bouquet and rejects it for another is a filter whose behaviour depends on
routing, which is exactly the kind of surprise that makes a black screen
undiagnosable.

Revisit only if a deployment needs two bouquets with different checksum
behaviour. Until then the operator turns a filter off for the whole server, and
TASK 2.3's plausibility scoring is the intended replacement for the blunt test.

`isnullDCW()` is deliberately not gated: an all-zero control word never decrypts
anything, so there is no valid key to rescue.

### D8 — TASK 1.4 keys trust off `src2string()`'s `(srctype, srcid)`

The origin audit (`docs/ORIGIN.md`) found that `src2string()`
(`main.c:877-979`) is already a complete resolver: all six source types, all
five cache origin flags, each with the right lookup and the right mask.

So the trust engine will **not** invent its own source identifier. It keys on
the same `(srctype, srcid)` pair that `src2string()` resolves, which guarantees
that the entity the dashboard names and the entity the trust score attaches to
are the same one. Any other choice would create two parallel identity schemes
that have to be kept in sync forever, and the failure mode is silent
misattribution — exactly what GR1 forbids.

Consequence worth stating: if `src2string()`'s mapping is ever changed, the
trust keys change with it and stored trust state is invalidated. That is
acceptable, because it fails safe — an unknown key scores as neutral, never as
trusted.

Related, from the same audit: **`ecm_setdcw` has two definitions**
(`setdcw.c:136` and `:789`) and only `:753` is compiled, because the build
defines `THREAD_DCW`. Any TASK 1.2 instrumentation placed in `:136` would be
dead code that looks correct and never runs.

### D9 — `checkcycle()` stays disabled until it can be observed, not just enabled

Both the definition (`clustredcache.c:499-513`, inside the comment `/*:498` to
`*/:514`) and its only call (`clustredcache.c:688`, inside the comment `/*:640`
to `*/:698`) are commented out, so neither is compiled.

The check itself is sound in intent: reject a cached control word whose CW cycle
does not match the ECM tag, which is exactly what a fake card that does not track
the cycle would produce. Reviving it would strengthen the cache.

**It is not being revived, deliberately.** It is the same class of change as the
filters TASK 1.10b just made switchable — a correctness test that can also
discard valid keys, and when it does, every channel on the affected bouquet goes
black while the server reports nothing. It has been off for at least 25
revisions, and we cannot currently measure how many valid CWs it would reject:
there is no counter for it, and `DCW STATS: ON` was never wired up.

Reviving it blind would risk reintroducing the exact failure mode this project
exists to remove, in exchange for a defence whose value is unmeasured.

**Precondition for reviving it:**

1. A rejection counter under `MCS_DCWSTATS`, running the check in *observe-only*
   mode — count and log, never reject.
2. A real deployment showing the rejection rate on known-valid keys is low.
3. Then gate it behind `DCWFILTER CYCLE:` defaulting to `OFF`, matching the
   TASK 1.10b options.

This is a sequencing decision, not a rejection. The check is worth having; it is
not worth having before we can see what it costs.

**AMENDMENT — FINAL, signed 2026-09-28 (TASK 4.4). This door is now closed, and
for stronger reasons than the ones above.** The question TASK 4.4 had to answer:
after TASK 3.6 unblocked the delivery path and Phase 2 built the observation
layers, is reviving `checkcycle()` now possible? Answer: **no — it stays
disabled permanently in this codebase.** Three grounds, each verified in the
tree today:

1. **The artifact is broken, not merely unmeasured.** `cwcycle.h` (TASK 2.5)
   records three structural defects, re-verified today:
   (a) its constants predate the three-valued `cwcycle_t`
   (`clustredcache.c:284`, `NO_CYCLE=0 / CW0CYCLE=1 / CW1CYCLE=2`), so its
   `cwcycle==0` branches test "declares nothing" while believing they test CW0;
   (b) the field its call site reads, `cwlist[i].cwcycle`, is written **nowhere**
   in the tree — zero assignments outside the comment itself (verified by
   grep over every `.cwcycle =` site: all write `pcache->cwcycle` or
   `cwdata->cwcycle`, never the `cwlist` member);
   (c) its return polarity contradicts its commented call site, which delivers
   when the function returns 1. Uncommenting it would reject the right keys and
   pass the stale ones *while looking confident* — i.e. it would manufacture the
   exact black screens this project exists to remove. D9's "cost unmeasured" is
   now "cost certain, benefit nil".
2. **The phenomenon it guarded is already enforced live on both real paths.**
   Demand side: `ecmdata.c:641-650` ("Setup Cw Cycle") drops an ECM whose tag
   contradicts the tracked cycle — stock, live. Supply side: the `Check Cycle`
   gate in `cache_setdcw()` (`clustredcache.c:1706-1710`) marks a contradictory
   declared cycle `DCW_ERROR` at ingest before any client — stock, live, and
   proven again by the `cyc` live target (contradictory mark refused, exactly
   one line, trust untouched). The only path the commented pair ever covered was
   delivery-time re-check of stored CWs, and TASK 2.5 now *measures* exactly
   that (`offered/marked/contra`, `handed/stale/unverified` in the
   STATS-WINDOW line) without rejecting anything, per GR3: a declared cycle is
   evidence, never proof.
3. **The poison defence this check hoped to add now lives in proof-based
   layers** — 1.5 purge on reuse proofs, 2.6 negative memory, 2.7 mirror,
   2.4 plausibility — all of which act on definitive proofs only (GR3). A
   delivery-time reject on a *declared* mark would be the one remaining
   punish-on-soft-evidence in the tree.

D9's original preconditions 1 and 3 are structurally satisfied today
(`MCS_DCWSTATS` counters; the TASK 3.15 per-profile `DCW FILTER` override that
realized 1.10b's gate), and precondition 2 remains unsatisfiable inside this
project (no real deployment) — but both are now moot because of ground 1.

**Ruling.** The commented definition (now at `clustredcache.c:607-625`; D9's
original numbers 499-513 drifted) and its commented call (inside the commented
function ending at `:826`; was :688) stay as historical text. No revival task
may be opened against this function. If a future real deployment's STATS-WINDOW
line shows sustained `stale > 0`, the remedy is a **new** check written against
the live rule (`cwcy_expect()` in `cwcycle.h`), gated like TASK 3.15 and counted
like `MCS_DCWSTATS`, proposed as a new decision — not a resurrection of this
one. Line numbers in the header comment of `cwcycle.h` (:512-526/:721) are its
own dated record and are intentionally left as written.

Signed: TASK 4.4, lead engineer under the delegation of 2026-09-22. Docs-only
change: no source file touched, binaries unchanged
(stock `2848850737c1e104507a0f992d9cd60b`, stats `ae406bff16d84af78c8ec600eeeb920d`).

### D10 — TASK 1.2 uses the existing per-client fields, not the ring buffer

The brief specifies a post-delivery retry **ring buffer**. The shipped TASK 1.2
does not add one, and that is a deliberate deviation rather than an omission.

While wiring the hook it turned out the fields it needs already exist and are
already maintained on every delivery (`srv-newcamd.c:881-827`):

| Need | Already present |
|---|---|
| the ECM identity to key on (GR6) | `lastecm.hash` — the field existed at `config.h:361` but was never assigned; now set |
| whether a CW was delivered at all (GR2) | `lastecm.status` |
| when it was delivered | `cli->lastdcwtime` |
| which source supplied it (GR1) | `lastecm.dcwsrctype` / `lastecm.dcwsrcid` |

So a ring buffer would have added roughly 136 bytes per client and a new writer
on the hot path to duplicate information that is already correct in place. It
also would have been a layout change to a hot struct, which D6 rules out without
a reason.

**What the ring buffer would still be needed for**, stated so this is revisited
rather than forgotten: attributing a retry after the client **zaps away and comes
back**. `lastecm` holds only the most recent delivery, so a client that fails on
channel A, zaps to B, and returns to A loses the origin of A's key. If TASK 1.6
(instant rotation) needs that history, `struct retry_ring` is already defined in
`retry_detect.h` and can be embedded without further design.

**Second part of the decision: observe-only.** The hook counts and logs; it
scores nothing and disables nothing. GR3 makes a false positive that disables a
good card worse than the original problem, so the detection has to be checked
against real traffic before TASK 1.4 may act on it. The counters are the
interface between the two tasks, and keeping them inert until then is what makes
that check possible.

### D11 — the reuse detector watches cache-exchange only, and that is not laziness

TASK 1.3 instruments the six cache-exchange call sites and deliberately skips the
CSP `TYPE_REPLY` path in `clustredcache.c`.

The reason is a data-availability fact, not a design preference: the CSP reply
decode reads `tag, sid, onid, caid, hash, provid` and **no `ecmd5`**. The
protocol carries a 32-bit hash only, and the request struct there is a stack
local, so its `ecmd5` field is uninitialised memory. D5 already ruled out
falling back to the 32-bit hash, because bucket collisions are not proof of
anything — and reuse is one of the three proofs GR3 lets trigger hard action.
Feeding it garbage would mean the one detector allowed to disable a card is the
one being driven by stack noise.

**Consequence, recorded so it is not mistaken for coverage that exists:** a
poisoned CSP peer is not caught by CW-reuse detection. It is still caught by
TASK 1.2's retry classification and, in Phase 2, by the agreement ledger and
timing forensics. What it is not caught by is the definitive proof.

> **SUPERSEDED — see D16.** The CSP path is now covered. The `ecmd5` this
> decision says is unavailable *is* available, from `pcache` rather than from the
> stack-local `req`. The reasoning above about `req` was correct; the conclusion
> drawn from it was not.

The same reasoning defers the Newcamd server path. `ecm_setdcw()` does have a
valid `ecmd5` on `ECM_DATA`, but it is also reached from two client-push sites
that do not, so it needs a per-caller validity flag before it can be hooked
safely. That per-caller validity flag is still not built; TASK 1.4 therefore
wired the trust engine to the same six cache-exchange sites as TASK 1.3, and the
Newcamd client-push paths remain uninstrumented.

---

### D12 — the trust engine is a scoring layer, and the score never acts alone

TASK 1.4 adds a per-source score on `(srctype, srcid, caid, provid, sid)`. Two
things are decided here because they constrain TASK 1.5 and 1.6 more than the
arithmetic does.

**1. A score is evidence, not a decision.** Nothing in 1.4 reads a score and
changes behaviour. The gate that turns `score < TRUST_ACTIONABLE` into an action
belongs to 1.5/1.6, and it gets its own config option and its own tests. Splitting
them this way is what keeps GR3 testable: right now the suite can assert that one
soft event leaves a fresh source at 35, above the actionable threshold of 25. The
moment a score and an action live in the same function, that assertion becomes
harder to write and easier to weaken.

**2. Decay is asymmetric on purpose, and the asymmetry is a number, not an
adjective.** Two soft events cost 37 points. One good result returns 2. That is
roughly 8:1 against a source, and it is deliberate: a source that is wrong twice
out of ten is worse than one that is right eight times out of ten is good, because
a wrong answer is immediately visible to the client and a right one is not
distinguishable from luck on a single request. The floor of 10 exists so the
penalty is never permanent, and the ceiling of 100 exists so a long record of
good answers cannot make a source immune to a single proof — one proof still
drops a ceiling source to 75.

**Rejected:** a symmetric step (±5, or whatever). It would let a high-volume
source wash out real evidence simply by answering often, which is exactly the
profile of a poisoned peer that also serves a lot of legitimate keys.

**Rejected:** evicting the lowest-scoring entry when the table fills. That would
make a busy server forget precisely the sources it has accumulated the most
evidence against. Eviction is longest-unseen instead.

**Rejected:** starting a new source at the ceiling. A source with no history is
believed, not trusted; `TRUST_START` is 60, which leaves room for two soft events
before it becomes actionable.

---

### D13 — purge marks, it never deletes; and the gate is the proof alone

TASK 1.5 is the first thing in this project that changes behaviour rather than
observing it, so both halves of it are decisions rather than details.

**1. Mark, do not unlink.** `cw_cache_data.status` already carries `DCW_ERROR`
(`clustredcache.c:286`) and the serving path already refuses a CW that has it
(`:1208`), with insertion short-circuiting on it too (`:1441`). "Do not serve
this CW" is therefore an existing, already-honoured bit. Setting it needs no
struct change, no list mutation and no new lock, so D6 holds without an exception.

Unlinking was rejected on the merits, not out of caution about touching things:
the bucket list is a *circular* MRU ring (`:394-460`), a removal has to fix `prev`
and `next` or the ring breaks, and a traversal written `for (p = tab[i]; p; p =
p->next)` over it never terminates. Marking gets the same client-visible result —
the poisoned CW stops being served — for none of that risk. Memory is not
reclaimed, and that is accepted: a cache entry is small and bounded, and the
alternative is a structural change to the hottest shared structure in the program.

**2. The gate is a proof, and only a proof.** `cache_purge_mark()` is called from
exactly one place per protocol, inside the `CWREUSE_PROOF` branch. There is no
score-driven path to a mark, and the header exposes no API that would create one.
This is GR3 read literally: a false positive that disables a good card is worse
than the black screen we are trying to remove.

**Rejected:** marking on `score < TRUST_ACTIONABLE`. It would have been the
faster route to visible effect, and it is exactly the route GR3 forbids. Two soft
events already put a fresh source at 23, below the actionable threshold — so a
score-gated purge would fire on two timeouts and a retry, with no proof of
anything.

**3. Scope is one channel, per GR4.** The sweep marks only the `(caid, provid,
sid)` the proof was for. A source that is provably bad on one channel is *not*
assumed bad on the others; widening is the trust score's job, applied by a later
layer with its own evidence. This deliberately under-reacts, which is the correct
side given GR3.

**The known limit, stated so it is not mistaken for coverage.** `cwdata->peerid`
records the *first* peer that reported a CW (`// fisrt peerid`, sic), with
`nbpeers` counting the rest. So the sweep can only reach CWs a source supplied
first. A poisoned CW that a good peer happened to report first is attributed to
the good peer and is not marked. This is why the gate is a proof: the narrower
the reach, the more the trigger has to be certain.

---

### D14 — rotation re-routes; it never disables, and its cap releases the newest

TASK 1.6 is driven by a *soft* signal — a client re-asking early — so GR3 forbids
treating it as proof. The decision is therefore about how far a soft signal is
allowed to reach.

**It may re-route. It may not disable.** `rotation_note_bad()` records a source
against a channel and `srvtab_arrange()` picks someone else. No score changes, no
card is blocked, no source is disconnected. The worst outcome of a false positive
is one request going to a different server, which is recoverable by construction.
Compare TASK 1.5, which acts on a proof and is allowed to stop a CW being served.

**Rejected:** feeding a soft retry into a hard action, however tempting. Two soft
events already put a fresh source below `TRUST_ACTIONABLE`, so a soft-driven
disable would fire on two timeouts and a retry, with no evidence about the key at
all.

**The cap releases the newest entries, not the oldest.** This is the part worth
keeping, because the opposite is the intuitive implementation and it is a bug:
stopping all avoidance once the cap is reached re-enables the *first* source
recorded — the one with the most evidence against it — exactly when the most
sources are failing. `rotation_should_avoid()` avoids only indices below
`ROTATION_MAX_AVOID`, so at least one recorded source always stays reachable and
the mechanism cannot strand a channel. A safety valve that fails closed is worse
than no valve.

**Expiry is evidence, not a clock.** A channel clears when a key is delivered and
the client does not retry. A timed expiry was rejected on the grounds that no
value works: too long serves more bad keys, too short re-tries the bad source
before the new one has been proven.

---

### D15 — the retry classifier is gated on "same channel", and that gate is a GR3 requirement, not an optimisation

**Context.** TASK 1.2's hook sits above upstream's `if (ecm)` guard in
`srv-newcamd.c`. Its first wiring gated the block on

```c
if ( (cli->lastecm.hash==ecm->hash) || !isnew ) {
```

which had two independent faults. It dereferenced `ecm` while NULL — always the
case for a profile's first ECM — and it segfaulted the server on that first ECM.
Separately, `!isnew` is logically identical to `ecm != NULL`, so the hash
comparison was dead and the gate reduced to "any ECM already in the table".

**Decision.** The gate is the same-channel test:

```c
int samechan = ( cli->lastecm.caid==clicd.caid )
            && ( cli->lastecm.prov==clicd.provid )
            && ( cli->lastecm.sid ==clicd.sid );
if ( samechan ) {
```

**Why the second fault mattered more than the crash.** The crash was loud. The
gate was quiet and wrong in the dangerous direction: it fires whenever the
requested ECM is already in the table, *including when another client put it
there*. A viewer who zaps 2 s after a perfectly good key would be classified
RETRY_HARD, and the source that supplied the good key scored down for a channel
change it had nothing to do with. That is the false positive GR3 names as worse
than the original problem, arriving through the most ordinary user action there
is. Any gate on this block has to answer "is this the same channel as the last
delivery", not "have we seen this ECM".

caid/provid/sid is upstream's own convention for that question — the freeze check
at `srv-newcamd.c:854` uses exactly this triple — so no new identity rule is
invented. GR6 is still met where it applies: the evidence stores key on the hash
and on caid/provid/sid, never on sid alone.

**Accepted cost.** A zap produces no GOOD event for the channel that was left,
because the next request is for a different channel. Trust scores therefore
recover only from clients that stay put. Under-reporting GOOD is the safe
direction; mis-scoring a working source is not.

**The general rule this sets.** Nothing inserted above upstream's `if (ecm)`
guard may read through `ecm`. The NULL case is not an edge case — it is every
profile's first request, which is the first thing any deployment does.
`make -C tests ncclient` now asserts the server survives both the `ecm == NULL`
and the `ecm != NULL` branch, and it was verified to fail when the old condition
is put back.


---

### D16 — the CSP cache path is hooked at the cache entry, not at the delivery point

**Context.** D11 left the CSP UDP cache protocol uncovered by CW-reuse detection
and purge, on the grounds that the reply carries no `ecmd5` and the request
struct there is a stack local. That is true of `req`. It is not true of the cache
entry: `pipe_cache_find()` copies `ecm->ecmd5` into `pcache->ecmd5` in both of its
branches (`clustredcache.c:675`, `:693`) under the same `#ifdef CACHEEX` that
declares the field. The identity was there the whole time.

**Decision.** Hook the `TYPE_REPLY` handler in `clustredcache.c`, reading
`pcache->ecmd5` via `cache_fetch(&req)`. Do **not** hook `ecm_setdcwdata()`.

**Why not the delivery point**, which looks cleaner because it is one place
instead of seven:

1. **Identity mismatch would manufacture proofs.** The six cache-exchange sites
   offer the ecmd5 that arrived on the wire; `ecm_setdcwdata()` would offer the
   locally computed one. If those ever disagree for the same ECM, one key looks
   like two and `cwreuse_offer()` returns PROOF. Reuse is the one event GR3 lets
   act on, so a false proof is worse than the poisoning being detected. Hooking
   only the CSP path, whose origins are flagged `PEER_CSP` and which none of the
   six sites ever sets, makes double-offering structurally impossible rather than
   improbable.
2. **It would add locking to the hot path.** `cwreuse_tab` is only ever touched
   under `prg.lockcache`. The CSP handler already holds it; `ecm_setdcwdata()`
   does not on the cache path, so hooking there means a new `lockcache`
   acquisition on every delivered CW.

**What was verified before writing it,** because each of these was an assumption
until checked: the `ecmd5` field exists only under `#ifdef CACHEEX`
(`ecmdata.h:63`); all three `malloc`s of an `ecm_request` are inside
`store_ecmdata`, which computes the MD5 unconditionally; `cwreuse_tab`,
`trust_tab` and `purge_tab` are defined in `main.c` at `:97`, `:113` and `:301`,
all before `#include "clustredcache.c"` at `:993`; and `MCS_CACHE_PURGE_WALK` is
defined at `clustredcache.c:403`, above the re-include that exposes
`cache_purge_sweep()`.

**Proven both ways.** `make -C tests cwreuse` asserts one proof and one purge when
a peer supplies the same key for two services, and **zero** of each when it
supplies the same key twice for one service. The second case is the point: a
client that cannot decode re-sends the ECM of the crypto period it is still in,
so one key arriving twice for one service is normal traffic. GR6 is what keeps
that from blacklisting a working source, and it is asserted rather than assumed.

---

## D17 — TASK 1.7: gate what the cache serves, not what it accepts; and give the trust table a lock of its own

**Decided 2026-09-24, under the standing delegation.**

**The policy line.** `TRUSTED-CACHE-FIRST` gates the four `PIPE_CACHE_FIND`
serve sites and nothing else. It does **not** gate `cache_setdcw()`, where a CW
that has just arrived from a peer is delivered to a waiting ECM. Dropping a live
push from an untrusted peer would silence that peer on soft evidence alone —
the hard action GR3 forbids — and at startup *every* peer is new. Acceptance is
governed by the min-peer floor (D9/1.11a) and, on a definitive proof, by the
purge mark (D13/1.5). A stored entry is the different case: it can be served
again and again for as long as it lives, which is how one poisoned push becomes
a permanent black screen on a channel.

**The asymmetry is the point.** Trusted source → cache-first, always. Distrusted
source with a consecutiveness-verified key → still served, because the pairing
with the client's previous key is evidence it is telling the truth and
suppressing it would break working setups. Distrusted source with nothing to
check against — a channel change — → deferred. The request with no cross-check
available is the one that gets the verified route.

**Unknown is trusted.** `trust_score()` returns -1 for a source never seen, and
that maps to ALLOW. A brand new peer has done nothing to earn suspicion.

**One proof is not enough to lose the fast path.** `TRUST_PROOF_STEP` is 25 and
`TRUST_START` is 60, so a single reuse proof lands on 35, still above
`TRUST_ACTIONABLE` (25); it takes two. A first draft of the unit test asserted
the opposite and was wrong. This is deliberate and complementary to 1.5, not a
gap in it: the *specific* poisoned key is retired by the purge mark, which
excludes it at all four sites through `DCW_ERROR` whether or not this option is
on. This option governs the source's standing on keys that were never proven
anything, and one proof is not yet a pattern.

**The trust table gets `trust_lock`.** It was written under `prg.lockecm` from
`srv-newcamd.c` and under `prg.lockcache` from the cache-exchange sites — two
mutexes, no mutual exclusion, a lost-update race on `score` and a window in
which `trust_find()` can probe a half-initialised entry. Not a crash (plain
integers, no pointer chased) but the score is the engine's only output. A
dedicated leaf mutex rather than reusing `prg.lockcache`: that lock is not held
on the `srv-newcamd.c` path, and acquiring it there would invert the existing
`lockecm -> lockcache` nesting. Ordering is now `lockecm -> trust_lock` and
`lockcache -> trust_lock`, and `trust_lock` is never held across another
acquisition, so no cycle is possible. This is a defect fix, independent of 1.7,
and should survive a rollback of everything else in the task.

**What was rejected.** Serving the cached key *and* sending the ECM to the card
servers as a cross-check, so a wrong key could be corrected inside one crypto
period. It cannot work: `ecm_setdcwdata()` returns immediately when
`ecm->dcwstatus==STAT_DCW_SUCCESS` (`setdcw.c:485`), so a second CW for the same
ECM is discarded and never reaches the client. The disagreement would have been
evidence for Phase 2's agreement ledger and nothing more, at the cost of a
server round trip on every cache hit.

**Testability finding worth keeping.** A `PIPE_CACHE_FIND` *serve* is
unreachable in a single-server harness, for a reason that is upstream behaviour
and not a harness limitation: `CACHE_FLAG_SENDPIPE` is set by both the FIND and
the REQUEST handler, FIND runs first, the flag is never cleared anywhere in the
tree, and `cache_setdcw()` marks `DCW_SENT` on every CW arriving afterwards. So
the entry has to arrive already populated — in production because another
server's client asked for that channel first. Any future test of the cache-first
path needs either a seeded peer push or two profiles, and the reason should not
have to be rediscovered.

---

## D18 — TASK 1.8: the structural pre-filters are evidence, and only evidence

**Decision.** The three upstream predicates that already gate delivery
(`checksumDCW`, `isbadDCW`, and the null/half-null tests) are left exactly as
they are. 1.8 adds a fourth thing beside them — `dcwstruct_scan()` — that
**rejects nothing** and changes no delivery decision. It returns a bitmask
describing why a key looks structurally impossible, the mask is logged once per
distinct key, and it moves a trust score by one soft event. If the mask were
ever turned into a filter, this task would have made black screens *more* likely,
not less: the checksum test alone already drops valid keys on bouquets that do
not carry it, which is precisely why TASK 1.10b had to add a runtime gate for it.

**Why the scan sits before `acceptDCW()` rather than after it.** A key that
`acceptDCW()` rejects leaves no trace anywhere: the client sees a timeout, the
operator sees nothing, and there is no way to tell which source kept sending it.
Evaluating first means a rejected key is attributed too, and since delivery is
unchanged the only cost is a handful of byte comparisons on a path that is
already comparing bytes.

**`DCWS_REPEAT` is deliberately excluded from the score.** `isbadDCW()` fires on
a uniformly random key with probability about 1 in 16 000. A busy cache peer
pushes tens of thousands of keys an hour, so on a *perfectly healthy* peer that
test alone produces several events an hour; scored, it would walk that peer to
the floor overnight and then re-route away from it — a systematic false positive
of exactly the kind GR3 exists to prevent. So the bit is still counted and still
logged, but only the six near-impossible shapes
(`SAME`, `MONO`, `LOWENT`, `HALVES`, `INV`, and a failed checksum *while that
filter is enabled*) may touch a score. A disabled filter's verdict is not
evidence, which is the rule that lets this layer stay permanently on.

**Per-half tests are skipped for an all-zero half.** An NDS key legitimately
carries one, and every per-half test would otherwise fire on it, on every key,
for every NDS bouquet. The cross-half tests are kept: an all-zero half paired
with a non-complementary half is not an NDS key, it is a forgery.

**An identical key is one fact, not one fact per delivery.** A peer re-pushes
the key it holds whenever it is asked, and a client on a stuck channel re-asks
every few seconds; both re-deliver the same 16 bytes. Each is a separate call
into the scan, so a small fixed table remembers the last few anomalies and
reports an identical `(source, channel, key, mask)` **once per minute**. The
window does **not** slide on each sighting: sliding would silence a source that
pushes the same impossible key forever, and "it is still doing it an hour later"
is the situation an operator most needs to hear about. Bounded at 8 entries,
`mcs/src/dcwstruct.h` to `main.c:220`. It is deliberately not a second copy of
upstream's rules: a duplicated checksum implementation is a second thing to keep
in step.

**Ordering consequence.** The scan runs on the ECM thread for every delivery, so
its table is protected by the same leaf lock the trust table got in D17, and
`trust_record()` — the lock-free core — is called directly rather than through
the `_lk` wrapper, which would take the non-recursive mutex a second time and
deadlock. Nothing else is acquired while that lock is held, and the log line is
written after the unlock so no thread waits on stdout inside it.

---

## D19 — TASK 1.9: the tolerance lives in the count, and the two ways a mark ends are counted apart

**Decision 1 — `BAD-CW-LIMIT` is a count, not a second list.** `rotation_entry`
keeps one slot per source holding the number of confirmed bad-CW events from that
source on that channel; a source is *avoided* when its count reaches the limit.
There is no separate "avoid list", because two structures that must agree are two
structures that can disagree. `rotation_note_bad()` and `rotation_should_avoid()`
keep their old signatures and delegate with `limit = 1`, which is the shipped
TASK 1.6 behaviour — so the 38 assertions written before this task still describe
them without being touched, and that is the evidence the refactor was faithful.

*What that caught.* The first cut stored counts for not-yet-avoided sources in
slots above `e->n`. Two sources producing bad keys alternately then shared one
slot, overwriting each other's count, so neither ever reached the limit and the
tolerance silently became "never" — a source that should have been re-routed
would have been kept forever. The unit suite's alternating-source case is what
found it, and it exists now because of it.

*Log discipline.* `rotation_note_bad_limited()` returns 2 only on the event that
reaches the limit, so the operator gets exactly one avoidance line per confirmed
event regardless of the tolerance. Below the limit the event is counted and
logged at a diagnostic level, never at the error level: nothing was acted on.

**Decision 2 — `SERVICE-BLACKLIST-TIME` closes TASK 1.5's write-only loop.**
`cache_purge_unmark()` and `cache_purge_should_withhold()` were both written for
TASK 1.5 and neither was ever called from outside the header: a mark could be
placed but never lifted, so the only exits were table eviction and nothing else.
Two exits now exist, and they are counted separately:
- **released** — the source delivered a key the client accepted, so *its own*
  mark is lifted (and only its own: another source's reuse proof still stands,
  GR1). Wired at the recovery branch in `srv-newcamd.c:636`, next to the
  `rotation_note_good()` call that already handled the same event for rotation.
- **expired** — the mark outlived the configured window and the ticker lifted it.

Keeping the two counters apart is the difference between "my peer recovered" and
"my peer went quiet and the timer ran out", which an operator reacts to very
differently. `max_age == 0` (the default) means no expiry, i.e. exactly what
TASK 1.5 shipped.

*Why not lazily, inside the withhold check.* It would have put a table write on
the serving path and a `GetTickCount()` inside a function whose whole value is
being a pure predicate. Expiry happens on the ticker; the hot path only compares.

**Decision 3 — `STATS-WINDOW` rides the cache thread's existing wakeup.** Not a
new thread, not a timer: `phase1_stats_tick()` is called from the 3-second
condition the cache thread already evaluates for peer pings. Granularity is
therefore one wakeup, which is documented rather than engineered around — a
thread that exists to print a line every N seconds is not worth the risk here.
The line is rendered by a pure `statsline_format()` so that what an operator
reads can be tested without a running server, the same split D18 used for the
structural scan.

**Coverage gap, recorded rather than papered over.** `BAD-CW-LIMIT`'s effect on
*routing* is not provable live today: `rotation_should_avoid_limited()` is
consulted in exactly one place, `srvtab_arrange()` in `loadbalance.c`, i.e. when
choosing a **card server**, and the cache path does not consult rotation at all
(`grep rotation clustredcache.c` returns zero hits). The harness has mock
clients and a mock cache peer, but no mock Newcamd *server*, so the live targets
prove the counting and the log discipline and the unit suite proves the
predicate. This was found by running a mutation the harness did **not** catch —
the only reliable way to learn what a test really bites on.

---

## D20 — TASK 2.1: what makes a disagreement evidence, and who gets accused

**The problem this answers.** Phase 1 could only ever say *something went wrong
after this key*. Every mechanism it has — the retry classifier, the trust score,
rotation, the purge marks — is built on one client re-asking, and that single
observation is genuinely ambiguous: a moved dish, a zap and a poisoned cache
entry are indistinguishable from one client's point of view. Raising the weight
of that inference trades one error for the other: more black screens stopped, and
more good sources punished. TASK 2.1 was the first chance to break the trade-off
by adding a *second* opinion instead of re-weighting the first one.

**Decision 1 — a disagreement alone accuses nobody; only the client can
accuse.** Two independent sources producing different keys for the same ECM
proves that one of them is wrong, and nothing more. It does not say which, and it
does not say that either is the reason a client is unhappy — a peer hashing a
different byte range, or a provider's second feed on a different key ladder,
produces exactly the same observation. So the ledger records the dispute, counts
it, and waits. What completes it is the only failure signal these protocols
have — the client returning for the same service in 1-3 s instead of at its ~10 s
crypto period — and then the two facts together are GR3's third definitive proof.

*Why this is not just caution.* A false positive here disables a working source,
which is worse than the original problem (GR3). The cost of waiting is bounded
and known: the client is already re-asking, TASK 1.6 already re-routes it, and
the ledger only upgrades the *hardness* of the action, not whether an action
happens. Nothing gets slower for anyone; the accusation gets more accurate.

**Decision 2 — the accused is the delivered source, never the dissenter.** A
disagreement identifies a pair of keys, not a culprit. The delivery clock
identifies the culprit: the key in use when the client failed is the key the
client received. So the accusation lands on that slot's source, and the source
that merely disagreed is named in the log and otherwise left alone — not
penalised, not promoted, because its key is simply unverified. This is GR1
applied from the other direction: attribution must follow the evidence, not the
convenience of whoever happens to be current.

*Live confirmation, in the harness rather than in prose:* the two peers race to
answer, and in a given run either one may deliver first. The target therefore
compares the accused source against the source named by the retry line in the
same log. A mutation that made the ledger accuse the dissenter instead failed
that assertion (`accused 1/65537 but the retry line names 1/65538`), which is the
only reason the assertion is worth having.

**Decision 3 — the key is the 128-bit ECM MD5, and a same-source contradiction is
not evidence of anything.** The reuse detector (TASK 1.3) already refused to fall
back to the 32-bit hash (D5), because that value is a bucket index: two ECMs
landing in one bucket would pair unrelated sources and manufacture a dispute out
of a collision. The ledger inherits the rule, which means it is inert in builds
without `CACHEEX` — no ecmd5, no identity, no ledger, and every caller behaves
exactly as it did before. A source that contradicts *itself* is counted
separately and proves nothing: there is no independent opinion in it, and that is
precisely what GR3's third proof names.

**Decision 4 — one proof per ECM, latched.** The same two keys for the same ECM
cannot yield a second, different conclusion, and a client failing repeatedly on
them is the same fact re-observed rather than new evidence. Without the latch,
every subsequent retry would re-accuse and re-log. When both keys later fail the
client, the first proof stands and the second is counted but not re-accused: the
source is already avoided, and a second hard action would buy nothing.

**Decision 5 — the tolerance does not apply, and the event is not counted
twice.** `BAD-CW-LIMIT` exists so an operator can require more than one accident
before a source is re-routed. A proof is not an accident, so rotation is asked to
avoid with limit 1 whatever the option says. For the same reason the retry emits
`TRUST_EV_PROOF` *instead of* `TRUST_EV_SOFT`: one observation, graded honestly,
rather than two events that would decay a score as if the source had failed
twice.

**Decision 6 — the delivered key is recorded, closing an upstream omission.**
`cli->lastecm.dcw` has existed since r82 and no protocol ever wrote it: every
`srv-*.c` fills in the source, the status and the channel at the delivery point
and leaves the key itself at zero. That was invisible until something needed to
name the key a client was actually holding — which is what this feature must
match a failure against — and the effect was total silence: every proof failed to
form, with no error, no counter and no log line anywhere. `srv-newcamd.c` now
records it where it records everything else, and clears it on the decode-failed
branch so a stale key can never sit next to a zeroed status (GR2).

**What is still missing, deliberately.** The live proof uses two cache peers as
its independent sources, because the harness has no mock Newcamd *server*. For
this feature that is not a substitute — it is the same thing: an independent
source is an independent source. The same gap does still hide the routing half of
TASK 1.9 (`rotation_should_avoid_limited()` is consulted only where card servers
are chosen), and a mock card server remains the work that closes both at once.
## D21 — TASK 2.2: the key that looks plausible, and why nothing else would have caught it

**The problem this answers.** Every detector shipped so far needs something to
happen *besides* the delivery. TASK 1.3 needs the same key twice for two ECMs.
TASK 2.1 needs a second source to disagree or a client to come back early. TASK
1.8 needs the existing filters to reject something. Phase 1's structural checks
(`checksumDCW()`, a SUM rule that is on by default, and `isbadDCW()`, repeated
triples per 4-byte quarter) do run on every key — but a key can be *invented* to
satisfy both, and then a poisoned cache entry looks statistically perfect: it is
served instantly, it is never cross-checked, and no client has failed yet. This
task adds the only two signatures that do not depend on a second event, because
they are properties of the key itself.

**Decision 1 — two cheap signatures, and the threshold is set by arithmetic, not
by taste.** `CWEN_LOWDIV` (at most 5 distinct byte values in the 16) and
`CWEN_NEAR` (within 16 bit flips of another key from the same source for a
different ECM). A genuine key has 16 distinct values with probability ~1, and two
unrelated keys differ by ~64 of 128 bits, so 5 distinct values has probability
3.4·10⁻¹⁸ and ≤16 flips has 3.2·10⁻¹⁹ — and the threshold sits at a quarter of
the genuine distance, which is why honest pairs do not drift into it. The forgery
the public r107 changelog describes (a peer that XORs 0xF0 into a real key's last
byte) is ~8 bits from the truth and lands in the middle of the band. The cost on
the hot path is one 16-byte loop and one popcount loop per delivery.

**Decision 2 — proximity is not attribution.** `NEAR_OTHER` (close to a key
another *source* delivered) is counted and logged, never scored. Two honest
sources behind one card farm can legitimately hold near-identical keys, and
scoring it would charge this source for another source's key — GR1 in its purest
form. `NEAR` requires both the same source *and* a different ECM: same ECM is a
contradiction, and contradictions belong to TASK 2.1, which has a proper
arbiter (the client) instead of a heuristic.

**Decision 3 — the identical key is counted, never judged here.** An identical
key for a different ECM is `CWEN_EXACT`: TASK 1.3's proven fact. It is counted so
the stats line adds up and *not* logged or scored, because the same event must
not accuse twice and the re-use detector has the evidence this one lacks (which
ECM came first, from where).

**Decision 4 — one key, one soft event, whatever fired on it.** A key that is
both low-diversity and 3 bits from another is a single accident, not two: the
verdicts collapse into one `TRUST_EV_SOFT`, so `BAD-CW-LIMIT` means what it says.
And it is a *soft* event even though the numbers look damning, because GR3's
asymmetry is the point of this whole project: a false positive that disables a
working card is worse than the black screen being prevented. Suspicion with no
definitive proof re-routes gradually; it never disables.

**Decision 5 — an all-zero half is protocol, not low entropy.** NDS sends a
genuinely null 8-byte half during transitions, so `LOWDIV` is skipped when either
half is all zero. A detector that fires on the protocol's normal shape would
spend its credibility in the first minute.

**Decision 6 — the harness holds a per-channel key instead of being restarted.**
The live target needs one peer to serve two *different* keys for two different
services, because a real cache peer's cache is per channel, not per peer. The
alternative — restarting the peer on the same port between phases — was rejected:
a restart is indistinguishable, server-side, from a peer that has gone away, so
the run could become a ping-timeout test that passes for the wrong reason.
`cachepeer.c` gained `CP_ALT_CW` for this: the first ECM fixes the identity and
every later, different ECM is answered from the second key. It also prints the
distance *it* intended, and the target asserts that number against the distance
the server measured, so the two sides can disagree out loud.

**Decision 7 — a test client's `$5` is the DES key, and the target varies the
SID.** This cost real time and is recorded so it cannot cost it again: the phase-2
client originally varied `$5` to ask for "a different ECM" and the server answered
with `wrong des key`, which reads exactly like a product defect and is a client
claiming a different newcamd key. A different *service* is what makes a different
ECM here, so a different SID is what the target changes, and the recipe says so
next to the variable. The second harness lesson is the same family: the stats
summary arrives on the cache thread's five-second wakeup, so asserting on it after
a fixed sleep tests the sleep, not the feature — the target now waits, bounded, for
the summary line that carries the number.

**What is still missing, deliberately.** `LOWDIV` is proven by the unit suite
(2 assertions fail when it is killed) and has **no live check**: the live target's
derived key has 16 distinct values on purpose, because a key that is both
low-diversity and 3 bits from another would confound the two verdicts the target
exists to separate. The gap is narrow and stated: both verdicts are computed in
the same call and reach the log and the trust engine through the same path, and
that path is covered live by `NEAR` (killing it fails 4 live checks). What remained
for Phase 2 was the rest of the inference layer; of that list, timing forensics
for Newcamd is delivered and decided in D22 below, and what is still open is
plausibility scoring, the cache-peer trust engine, cache protocol hardening, and
the trust lifecycle (probability, probation, persistence).

## D22 — TASK 2.3: what timing can and cannot prove, and the number that had to be measured

**The problem this answers.** Phase 2 had three detectors, and all three need
something to compare against: TASK 1.3 needs the same key twice, TASK 2.1 needs a
second source or a failing client, TASK 2.2 needs the key to be arithmetically
absurd. A fabricated key that is *plausible*, served instantly and never
contradicted, defeats all three — and that is the cheapest way to cause a black
screen. Timing is the one property a table lookup cannot fake, because a card has
to do the work. So this task reads the number upstream was already computing.

**Decision 1 — the verdict is a physical statement, not a statistical one.**
`CWT_FAST`: the reply arrived faster than `MIN-CARD-LATENCY`, an operator-set
floor. That is a claim about what a card can do, and it is the only claim in this
layer allowed to move a score. There is deliberately no "suspiciously fast"
heuristic: a threshold someone tuned by feel would decay a score on a judgement,
and the whole point of this layer is that it only says things that are true.

**Decision 2 — regularity is evidence for the operator, never for the score.**
`CWT_DEGEN` (a full window inside `CWT_FLAT_SPREAD`) is printed and counted and
kept out of `CWT_SCOREMASK`, because real hardware can be perfectly regular — a
card reader on the same host, a server on the same machine — and GR3 is explicit
that costing a good card is worse than the black screen being prevented. The log
line says so in words, so it cannot be read as an accusation by someone who did
not read this document.

**Decision 3 — the floor ships OFF, and the operator turns it on.** "This answer
was not produced by a card" is only meaningful when someone has declared that the
slot is supposed to be a card. A Newcamd server that is itself a proxy with a
cache answers in under a millisecond, legitimately, for channels one of its own
clients already opened; scoring that by default would punish a correct
configuration. `MIN-CARD-LATENCY: 0` is off; the live target sets 42 ms on
purpose.

**Decision 4 — the hook lives in `cli-newcamd.c` and nowhere else.** GR5 says a
cache hit is instant by design, and a client-pushed key was never produced by a
card at all, so latency carries information for card servers and for nothing
else. Putting the call at the single place where a card server's reply is
processed makes that structural: the next protocol someone adds cannot
accidentally be timed.

**Decision 5 — `CWT_FLAT_SPREAD` is a measured constant, and the first value was
wrong.** It shipped at 1 ms. The live run then showed this path measuring a
genuinely constant reply as 30, 31, 31, 31, 31, 30, 30, 36 ms and a 50 ms
metronome as 51..52 with an occasional 60: a spread of 6–9 ms on identical input,
because the measurement passes through MultiCS's own card-server reader. A 1 ms
band sits below the noise floor of the path that feeds it and would have fired by
luck. It is now 5 ms — above the measured noise, far below the 20 ms of honest
jitter the same run produced — and the unit suite pins the constant with the
reason next to the assertion, so changing it has to be deliberate.

**Decision 6 — the same measurement set the harness levels, not taste.** The
floor in the live target is 42 ms: above the 30–36 ms a table lookup produces on
this path and below the 51–52 ms a scheduled 50 ms reply produces, with both
margins stated in the target. When a later change makes these numbers wrong, the
target fails and names the number, instead of failing for a reason nobody can
reconstruct.

**Decision 7 — a new harness, because the missing piece was the card server
itself.** `tests/ncserver.c` answers the newcamd handshake with the server's own
crypto and replies to each ECM after a scheduled delay. It exists because a
verdict about how long a card takes cannot be tested by a harness that cannot
pretend to be one — and it incidentally supplies what TASK 1.9's routing half has
been missing since Phase 1: something MultiCS will accept as a *decoder*, so
`rotation_should_avoid_limited()` can finally be observed live.

**Three defects found by running it, recorded because two of them are traps for
whoever writes the next harness.** (1) `parse_hex()` reads a whole run of hex
digits, so a DES key in the config must be 14 separate two-digit tokens; one
28-character token produces `Error reading DES-KEY` and no server at all.
(2) The mock's `LOGIN_ACK` has to be encrypted with the **keymod-derived** key and
the passwd-derived key swapped in only afterwards (`srv-newcamd.c:181-190`);
doing it in the other order made the client close the connection, which looks
like a crash and looks nothing like a key. (3) Enabling `DBG_SERVER` through the
HTTP debug hook *silences* every line this project adds, because `flagdebug` is a
filter and our evidence is `DBG_ERROR`; a test that turns debugging on has
switched its own eyes off, so the target sets it back to `ALL` (`0`) and asserts
a stats line returns.

**What is still missing, deliberately.** No timeout judgement: a source that
never answers is GR2's business and has its own counter, and charging it for
lateness here would double-count one fact. No per-server trust: the history is
per card server (that is what is timed), but the event lands on the
`(source_type, source_id, CAID, PROVID, SID)` slot of the channel that was served
(GR4). And no use of the *spread* as a discriminator in either direction beyond
the documented band — the plausibility-scoring task that remains in Phase 2 is
where a longer, statistical view belongs, if it belongs anywhere.

## D23 — TASK 2.3a: where a startup validation may live, and what a reload may do to a running server

**What forced the question.** A config with no profile section crashed the server
with a bare SIGSEGV — `get_cache_caids()` read `cfg->cardserver->card.caid`
without testing it, and `check_config()` calls that first. The crash was
one-run-in-thirty, because the guard that was supposed to report the problem
races the thread that causes it.

**Decision 1 — a validation lives where the data it validates is produced, not
where it is consumed.** `main.c`'s check was after `start_thread_config()`, and
that call only *creates* the config thread: the profile list is filled inside it.
main cannot know when parsing finished, so it slept 100 ms and hoped, while the
config thread slept the same 100 ms one line before `check_config()`. Two equal
sleeps is a coin toss, and the losing side produced a crash with no diagnostic —
the crash handler is compiled out unless `SIG_HANDLER` is defined and needs a
writable `debug_file` even then. The check now runs in the config thread,
immediately after `read_config()`. The general rule this establishes: if a
validation is only correct under an assumption about another thread's progress,
it belongs in that other thread.

**Decision 2 — the consumer is fixed too, and not only the guard.** The guard
alone would have left the same NULL dereference reachable from the reload path,
where there is no guard at all. `get_cache_caids()` now treats an empty profile
list as "no caids known": the loop below it already tested `cs` on every
iteration, so this is the function's own intent, not a new policy.

**Decision 3 — a reload that loses its profile section must not kill the
process.** Truncate-then-save in an editor makes an empty config easy to produce
by accident, and a server that dies on it is a black screen caused by the tool
meant to fix it. The reload branch logs
`!!! CONFIG: reloaded config has no profile section` at `DBG_ERROR` and carries
on. What the rest of the reload path does with zero profiles — stale listening
sockets, freed profile structs — has deliberately **not** been changed blind: it
is recorded as a known gap and belongs with the config work in Phase 3's
STRICT-MODE, where the reload path gets its own audit. Graceful degradation means
the server does not die; it does not mean the operator is not told.

**Decision 4 — when the debugger is missing, make the race deterministic rather
than run the loop more times.** 200 runs with a `SIGSEGV`-backtrace shim produced
nothing, because the shim's own startup work re-timed the race away. Interposing
`usleep` and shortening it *only in non-main threads* made the config thread win
every time, the pre-fix binary crashed 5 times out of 5 with the stack
`reread_config_thread → check_config → get_cache_caids`, and the fixed binary
exited 1 in 100 consecutive runs under the identical forcing. A test that cannot
fail is not evidence; a forcing that makes the bug certain is.

## D24 — TASK 2.4: what a key's shape may be used to conclude, and behind which gate the evidence hooks actually stand

**The problem.** `acceptDCW()` asks a yes/no question (`checksumDCW()`, four group
sums) and its answer covers three different situations: a stub, noise, and a key
that is perfect except for one byte. The third is the forgery this project's own
research turned up — r107 warns of a peer that XORs the last CW byte — and a
blunt yes/no cannot distinguish it from the other two.

**Decision 1 — grade the answer, and make the grade itself the evidence.**
3 of 4 groups intact means exactly one byte's worth of damage in an otherwise
structurally correct key. That is a statement about *the key*, not about the
operator's configuration, and it is why this layer needs no `DCWFILTER` gate to
lean on: 3/4 means the same thing whether the blunt test is on or off. The price
is a weaker per-key claim, and the price of *that* is latency: the first damaged
key is counted, the third is what convicts.

**Decision 2 — one key is never enough, and the number is derived, not chosen.**
A key from a bouquet that does not use the SUM rule lands on exactly 3/4 with
probability 1.52 %; with a thousand keys a minute, a busy honest source produces
fifteen of them. So the verdict needs a pattern: ≥3 one-group keys, in one 60 s
window, at ≥20 % of the source's traffic, all failing the SAME group. Worst
honest case 3.2e-5 per window (≈ once in nine hours of continuous traffic); a
forger that edits every key is convicted on the third one. The same-group clause
is what makes the claim about one damaged byte instead of four separate accidents,
and it is the clause that keeps a general "random-looking key" heuristic from
creeping in — nothing here is scored for being *unlikely*, only for being
*edited*.

**Decision 3 — `CWP_WEAK` (≤2 groups) is counted and never scored.** It is
simultaneously the strongest-looking signal and the least meaningful: an honest
bouquet that ignores the rule produces it on every key. Scoring it would decay a
correctly configured source continuously, which is precisely the failure GR3 calls
worse than the original problem.

**Decision 4 — a source, not a service, is what a rate can be measured over.**
The table and the window are keyed on `(srctype, srcid)`; the event carries the
`(CAID, PROVID, SID)` that was being served. GR4 is untouched because the score
still lands per service; the rate simply cannot be per service, since one damaged
key on one channel says nothing about a source until it is compared with the rest
of that source's traffic.

**Decision 5 — the cache gate is upstream of every evidence hook, and that is a
finding about the *architecture*, not about this task.** `cache_setdcw()` calls
`acceptDCW()` first and returns −1; the hooks live in `setdcw.c` above *its own*
gate, so a key from a card server is examined before being discarded and a key
from the cache is never examined at all. This was measured, not reasoned: four
forged keys, four replies, zero deliveries, `0 full, 0 one-group` — the layer was
blind, not quiet. This task closes it for plausibility at the gate itself, on the
rejecting path only, so a passing key is still counted exactly once. **TASK 1.8
and TASK 2.2 have the same blind spot and it is deliberately left open**: fixing
them changes two more layers' coverage, needs its own tests, and hiding that
inside this task would be the kind of silent scope creep this project forbids.

**Decision 6 — the harness must be able to be a *plausible* forger.**
`CP_FRESH_KEY` (a different, valid-looking key per reply) exists because the first
target had one peer answer four ECMs with one key — which is, correctly, a CW-reuse
proof to TASK 1.3: the cache was purged, the requests were re-routed, the client
was never answered. A layer whose job is to catch a careful forger cannot be tested
with a careless one. `CP_FORGED_FRESH_EVERY=N` then exists so the restraint phase
can be built from a source that damages one key in eleven and is otherwise honest
— a case no real deployment can produce on demand and the one most likely to
produce a false positive.

## D25 — TASK 2.5: the cycle question is measured before it is enforced, and silence is never read as guilt

**The problem.** The fifth class of well-formed non-opening CW had no witness at
all: a key from the *previous* crypto period, or the wrong half of the
even/odd pair, is sixteen structurally perfect bytes — checksum intact (2.4
quiet), entropy healthy (2.2 quiet), no second source to disagree with (2.1
quiet), no SID change for 1.3 to see (deliberately). The protocol carries the
answer end to end — the ECM declares which half it wants, a forwarding peer
declares which half it sent — but the tree's only consumer of that pair of
facts, `checkcycle()`, has been dead since 1.11b for three concrete defects:
its constants predate the `cwcycle_t` enum (so `cwcycle == 0` reads "declares
nothing" as if it were CW0), the `cwlist[i].cwcycle` field it reads is written
nowhere in the tree, and its return polarity contradicts its own commented call
site. Re-enabling it would reject right keys and accept stale ones while
looking authoritative.

**Decision 1 — take the rule from the live code, not from the comment.** The
expectation is written twice in live code already: `put_ecm2cache()`
(`clustredcache.c:656`) and the "Setup Cw Cycle" block (`ecmdata.c:641`). Both
say the same thing: the expected half is a function of the ECM's own tag byte
against the channel's `cw1cycle`, and a channel that declares nothing expects
nothing. `cwcy_expect()` writes that rule once. If one of the two live copies
ever changes, the unit suite's rule cases are where the drift shows first.

**Decision 2 — observe only, and say so in the sentence itself.** No rejection,
no delay, no alteration, and — the part that needed deciding — **no trust
event**. A declared cycle that contradicts the request is strong *evidence*
about a peer, and GR3 exists precisely because strong evidence is not proof:
the same marker byte can be wrong because the peer's own upstream is a mesh of
forwarders whose `fwd` flags disagree. What the existing upstream gate does to
a contradicting key (refuse it, `DCW_ERROR`) it keeps doing unchanged — 2.5
only makes that behaviour *countable*, so the next decision (what a stale key
costs a peer, whether the marker should be requested from every peer) is taken
on a measured base rate instead of a guess. The log line ends with "this is a
measurement, not an accusation" because a line that looks like an action but is
not one is worse than either.

**Decision 3 — silence is a different counter.** Most peers in a real mesh send
no marker at all (the byte rides only when `received==30` from a `fwd` peer).
Reading silence as guilt would condemn the majority of a healthy mesh on every
key. So the layer keeps three verdicts strictly apart — agree, contradict,
unjudged — and the hand-off side reports *unverified* (the ECM declared a half,
nobody declared the key's) as its own number, because that is the honest size of
the exposure window, not an accusation. The live target's phase 3 pins it: an
undeclared key grows "offered" but not "marked", changes nothing else, and
writes nothing.

**Decision 4 — the marker travels the pipes; the key bytes are never consulted.**
Byte 15 of a control word is key material (the fourth group's checksum byte),
not a declaration. The declaration lives *outside* the key on every hop, so 2.5
appends one byte to each of the two internal pipe messages (cache→ECM, and the
setdcw pipe) rather than deriving anything from the key itself. The buffers
absorb it (41 of 48, 34 of 64 bytes), the pipes are internal to the one
translation unit, and `ecm_setdcw_marked()` is additive: every existing caller
keeps `ecm_setdcw()` and passes no marker, because card servers and cache-ex
flavours in this revision genuinely declare none.

**Decision 5 — attribution is the source, and both watchers funnel through one
locked wrapper.** The offer is observed in the cache thread, the hand-off in the
setdcw thread (`THREAD_DCW` is our build); both call `ccy_note()` in `main.c`,
so one leaf mutex (`cwcy_lock`, held only across counter arithmetic, never
across a log write or the trust lock) covers the table and the lifetime
counters. The slot key is `(srctype, srcid)` — CSP peers, cache-ex CCCam,
Camd35 and Cs378x clients each land in their own slot via their flag bits
(GR1/GR4) — and a cache-ex offer is `NO_CYCLE` by protocol, which lands it
unjudged, exactly as honest as it is.

## D26 — TASK 2.6: the memory is about the BYTES, and the scope was decided by the target failing the right way

**The problem.** Both definitive proofs (the ledger's dispute-plus-failure and
the reuse detector) convicted a KEY, but everything the project did with the
conviction was shaped around a SOURCE: the 1.5 mark is filed per origin, the
sweep touches only keys whose `cwdata->peerid` is that origin, and nothing at
all consults any of it at OFFER time. The same 16 bytes, re-offered by a
relayer or after the entry expired, went straight back to a client. The proof
was real; the memory was not.

**Decision 1 — remember bytes, not sources.** The slot identity is a 64-bit
FNV-1a digest of the 16 key bytes plus (CAID, PROVID). No origin, no score, no
expiry: a proof does not go stale, the table is bounded by LRU instead, and a
full table always evicts rather than refusing the next proof — the next proof
is exactly the one thing a full table must not lose. The origin of the proof
and the proof time are stored as EVIDENCE for the line, never as a condition.

**Decision 2 — the scope was corrected by measurement, and the correction is
recorded.** The first implementation scoped the memory to (CAID, PROVID, SID),
on the GR6-adjacent reasoning that a channel is a SID. The live target's first
run failed the right way: service 2's proof filed the key under service 2's
SID, and service 3's offer of the identical bytes sailed past the memory and
delivered a second black screen. The lesson is transponder reality — one CW
stream is shared by every service of a mux, so a key that failed a client on
one service is the wrong-period key for all of them; and the SID changes with
every zap while the poison does not. The identity is now (key digest, CAID,
PROVID), and GR6 is honoured the correct way round: the key digest is the
primary discriminator, so two streams sharing a SID cannot suppress each
other's live keys — that would require their live keys to be byte-identical to
a value that already failed a client. The failing run is preserved as the
lesson in the header comment and in this entry, not deleted.

**Decision 3 — marks only where proofs already act, gates only where offers
already arrive.** Eight mark taps: the ledger's PROOF branch (the key the
client actually held and failed on) and the seven reuse-proof sites (CSP plus
six cache-ex flavours). Zero new proof logic — if a proof fires, the mark is
one line beside the actions it already takes; if nothing fires, this file is
inert. One gate: `cache_setdcw()`, after the plausibility note (2.4's counting
of refused keys is unchanged) and before the entry lookup (a refused key costs
nothing: no storage, no peer-agreement increment, no delivery path at all).

**Decision 4 — the refusal is an end in itself and says so.** No trust event,
no rotation, no purge at refusal time: the proof that filed the key already
did all of that to the origin, and GR3 forbids piling a second consequence on
the same evidence. The refusal line ends with "not a score: nothing was scored
and no source was disabled" because the operator must be able to tell a
memory from an action. The one behavioural guarantee is absolute and narrow:
THESE bytes, on THIS provider, will not open a picture for anyone again.

**Decision 5 — multi-thread, one leaf lock.** Proofs arrive on the cache
thread (CSP), cache-ex client threads, card-server threads and the ledger
path; the gate runs in the cache thread under `prg.lockcache`. `cwneg_lock`
covers the table and the counters, is held only across the arithmetic, and
takes no other lock — it cannot participate in any lock order. The stats
line's live count takes the same leaf lock on the stats tick, off the ECM
path.


## D27 — TASK 2.7: the complement-mirror check reads the key's construction, not its quality (2026-09-25)

**Trigger.** The externally sourced plan claimed mirror CWs "pass the checksum
because half 2 is the complement of half 1". Verified against the real
`checksumDCW` before adoption, and the claim came out REFINED: a pure mirror
fails two of the four group sums by exactly 2 each ((253-S) vs (255-S)), so
the checksum filter already refuses it and 2.4 already counts it weak. The
construction that actually walks past every layer is the REPAIRED mirror:
half 2 = ~half 1 with the two broken sums fixed afterwards. /tmp/mirror_math.c
proved it passes all four sums and is delivered silently; a 50,000,000-key
probe found 0 random sum-valid keys with >= 4/6 complement pairs, so the
pattern is not produced by chance.

**Decision 1 — checksum bytes are never paired.** The scan reads the six FREE
byte pairs (0,8)(1,9)(2,10)(4,12)(5,13)(6,14). Bytes 3, 7, 11, 15 are sums by
definition; pairing them would make every well-formed key read as a mirror.
With free bytes only, a genuine key has six independent bytes on each side,
and five or six complements among them has probability ~0 (the 50M probe).

**Decision 2 — observe-only, conviction by pattern, one line per window.**
No `cm_note` call can reject, delay or alter a delivery; the only outputs are
a counter and, at most, one `TRUST_EV_SOFT`. A single mirror key is counted,
never scored (GR3: one soft observation never convicts — the same rule 2.4
set). The pattern needs >= 3 mirror keys AND >= 20 % of the source's 60 s
window: a source that sends three mirror keys inside three seconds of mixed
traffic is not flagged until the share crosses, because the point is a
CONSTRUCTION HABIT, not a one-off collision. The line names the construction,
the pair count, the reason the checksum layer passed it, and ends with
"Delivery was not touched by this layer" — the operator must be able to tell
an observation from an action.

**Decision 3 — identity (source_type, source_id, CAID); SID excluded on
purpose.** The habit is the SENDER's, not the service's: the same peer
mirror-building one service mirror-builds the next. SID stays in the line as
evidence. This is GR4 read forward — finer than per-server, coarser than
per-key — and matches how the pattern is meant to be used by the coming
trust engine (per-source, per-CAID soft evidence).

**Decision 4 — three evidence hooks, no gate.** `cm_note` in both `setdcw.c`
bodies (live + dead, above the accept gates, reading no filter gate: the
construction means the same thing either way) and on the `cache_setdcw`
reject path (the one place a PURE mirror is seen after the checksum filter
refused it). The repaired mirror is delivered, so its story is told at the
setdcw hooks; the pure mirror is refused, so its story is told at the cache
hook. Same leaf-lock discipline as D26: `cwcm_lock` covers table + counters,
takes no other lock, cannot participate in any lock order.

**Lesson kept.** The external plan was right that mirrors matter and wrong
about which variant matters. The arithmetic was checked against the real
checksum before a single line of the layer was written — the same discipline
that caught 2.6's SID-scoped mistake before it shipped.


## D28 — TASK 2.8: two tiers of the same truth — the coarse tier sums a sender's habit, and its only consequence is which peers we ask (2026-09-25)

**Trigger.** Phase 2 built five detectors and two proofs, and every one of
them writes the five-part key (source, CAID, PROVID, SID). That is the right
granularity for a delivery decision and the wrong one for the question "who
is this counterparty": a peer poisoning fifty services leaves fifty entries
with a scratch on each, nobody sums a sender's record, the operator has no
per-peer standing anywhere, and `cache_send_request()` asked every peer
blindly. The cache-peer trust engine closes that at the lowest possible
risk: the SAME events, the SAME arithmetic, one level coarser.

**Decision 1 — the fine tier stays the authority; the coarse tier can only
shape ASKING.** Nothing in trustagg can reject, delay or alter a delivery.
The one consequence is wired into the cache request fan-out: a peer below
TRUST_ACTIONABLE on a CAID does not get ASKED for that CAID, for one 60 s
window, then exactly one request tests it again. Pushes and replies are
never gated. The standing rule holds: no auto peer disconnect — a skipped
peer still sees every push, still answers every offer, and one request per
window keeps GOOD events possible, which is how it wins its share back.

**Decision 2 — the coarse key is (source, CAID), and that is GR4-compliant.**
GR4 forbids scoring "a server" as one blob. (source, CAID) is finer than a
server; the CAID is kept precisely so a peer good on one bouquet family is
never condemned by another's evidence, which the live target proves (a
second CAID would have its own entry and its own line). PROVID and SID are
deliberately out: the habit is the SENDER's, and splitting them would
rebuild the fifty-scratches problem one level up.

**Decision 3 — the arithmetic is trust.h's, imported, not reinvented.** Same
SOFT halving, same flat PROOF step of 25, same slow GOOD climb, same floor,
ceiling and TRUST_ACTIONABLE line. A coarse verdict must never argue with a
fine one about what counts as bad. The crossing that arms the brake and
writes the line is edge-triggered with a per-entry 60 s window: one sentence
per peer per window while it stays below the line, re-armed by the next
event after the window — a still-poisoning peer is caught the moment it
speaks again, a silent one costs nothing.

**Decision 4 — the brake ships OFF.** `PEER-TRUST-REQUESTS: ON` arms it;
default OFF means stock request behaviour bit for bit. Everything else —
the aggregation, the crossing sentence, the stats — runs either way,
because evidence never costs anything. This is the 1.7 doctrine applied to
the request path: the operator turns the routing consequence on, and turns
it off with one config line (GR8's reversibility at the config level).

**Decision 5 — locking is inherited, not invented.** The taps sit beside
existing trust_record()/trust_record_lk() calls and share trust_lock; the
gate runs on the cache thread under lockcache -> trust_lock, the order
cachepref_gate() already established. No new lock, no new lock order, no
way to join a cycle. The table is fixed .bss; LRU eviction mirrors trust.h
(oldest loses the slot, never the worst-scored).

**Lesson kept.** The first live run of the pt target crossed on SOFT events
produced by the rig's own key: a fixed poison key whose two halves were
IDENTICAL bytes is low-diversity (2.2) by construction, and two of those on
one coarse entry is a crossing. The harness key was replaced with sixteen
distinct bytes, all sums valid, zero complement pairs — a poison key that
no structural detector may have an opinion about, so the target proves the
ONE thing it exists to prove.


## D29 — TASK 2.9: the protocol guard drops only what cannot be decoded, and says who is knocking (2026-09-25)

**Trigger.** A read of `cache_recvmsg()` with the brief's "cache-heavy
deployment" in mind found two holes that none of the Phase-2 layers could
see, because they are below every layer: five packet types read offsets the
datagram may not carry (`received` floor is 2), deciding on uninitialised
stack memory — a REQUEST keyed by a garbage hash creates a cache entry that
answers to nothing, a PINGREQ anchors a peer at a garbage port, and under
CACHE AUTOADD that garbage port becomes a peer; and packets from
unconfigured senders vanished at `if (!peer) break;` uncounted and unseen.

**Decision 1 — uninterpretable is not decidable: drop before any parse.**
The per-type minimums in cacheguard.h are the offsets the handlers actually
read — a fact, not a policy — and unknown types keep the stock floor of 2,
because inventing a minimum for packet shapes this server does not speak
would be exactly the over-reach GR3 forbids elsewhere. Dropping a datagram
too short to decode is not a judgement about a key or a source: GR3's
shape-signal rule does not apply, and no well-formed sender can ever see the
gate move (the mins are satisfied by this server's own senders: REQUEST 12,
REPLY 29/30, PINGREQ 13, PINGRPL 9, RESENDREQ 16, HELLO_ACK 9).

**Decision 2 — silence is its own defect: count the knockers, name them,
change nothing.** The unconfigured-sender path keeps stock's drop; what it
gains is a counter and one line per window naming the sender and the packet.
Most dark senders are misconfigured legitimate peers — wrong port, NAT
rebinding — and "why is my peer not sharing?" finally has an answer in the
log. The line says stock dropped it before any layer saw it, so nobody
reads a new behaviour into it.

**Decision 3 — one line per kind per window, counters always.** The same
derived discipline as 2.4/2.7: the first event in a window writes and states
its own suppression ("N in this window, counted in the stats only"), the rest
are counted silently. A flood of malformed datagrams produces one line a
minute and an honest counter — bounded, GR9, and still enough for an
operator watching live.

**Decision 4 — no lock, on purpose.** The hooks run in cache_recvmsg() on
the cache thread; the stats snapshot that reads the counters runs on the same
thread's tick. A guard that added a lock to the hottest ingress in the
server would cost more than the defect it fixes.

**The r107 claim, checked and closed.** The external changelog advertises an
"intolerant cache/ex filter: send replies only if cw cycles or detected as
not fake". Our tree does not need it: `cache_setdcw()` already refuses
non-`acceptDCW()` keys at INGEST — strictly stronger than filtering at reply
time, because the poison never enters the cache at all. Verified, no code,
recorded here so the claim does not come back as new work.

**Lesson kept.** The first live run's line read "from 1.0.0.127" — the
formatter printed the address MSB-first while the whole server (iptoa)
prints LSB-first off raw sin_addr. The formatters were fixed to the
project's convention and a unit check now pins the byte order by name.

---

## D30 — TASK 2.10, the trust lifecycle: the fade and the file (2026-09-25)

**Decision 1 — evidence relaxes, convictions do not.** The fine and coarse
tiers fade toward TRUST_START, one point per step of silence after a grace,
from either side (a ceiling decays down — standing is not a possession, it
is a reading of recent behaviour). Counters never fade, negative memory
never fades (D26 stands: no TTL, on purpose). A grace that never expires
would brand a peer for the process lifetime; a fade that touched negative
memory would launder a proven key by silence. The line is drawn exactly
there.

**Decision 2 — the fade is stateless.** No timer, no background ageing
pass over entries: the fade is recomputed from (now, lastseen) whenever the
3-second tick looks, idempotent within a tick, and a fresh event re-shields
by moving lastseen alone. Nothing to desynchronise, nothing to persist
about the fade itself.

**Decision 3 — persistence OFF by default, plain text, ages not ticks.**
The snapshot is a plain-text file an operator can read (and remove) without
a tool: TF/TA/NG lines, identities + score + counters + age. It carries no
ticks — the loader re-bases ages onto this boot's counter and pins at 0
when the file is older than the boot (conservative: an old entry behaves
like an idle one, it does not get a fake fresh event). Default OFF because
every behaviour switch here ships off; the fade itself is default-on with a
generous 1800 s grace because it only relaxes, and relaxing toward START is
the system's null state.

**Decision 4 — load once, before any thread can act.** The restore runs in
the config thread right after the first config parse and before
inotify/server threads exist, guarded to run exactly once per process: a
SIGHUP reread never re-reads the state file (a reload must not resurrect or
wipe standing behind a running server). The write side is tmp+rename from
the cache thread's tick — never the ECM path (GR9) — and a failed write
counts and keeps the last good file.

**Decision 5 — strict parse, skip whole.** A state line that fails
validation (wrong kind, wrong field count, score outside [FLOOR,CEILING],
negative counters, bad hex, age 0 or over 7 days) is counted, named in the
load log as "unusable", and skipped entire. Half-applied trust is worse
than no trust. The `NG` line carries the full 8-byte evidence prefix in
hex: the operator can see exactly which key the server will refuse.

**Verified live** (`make tl`, 15/15): standing written, faded past the
grace, snapshotted (TF/TA/NG), the server killed and restarted with a
garbage line appended — the loader restored 2 fine, 1 coarse, 1 negative,
refused the garbage line whole, and the very first fresh offer of the same
poison key was refused at ingest, with no second mark filed.

---

## D31 — TASK 3.1, the HTTP request surface: one TU, six bounds (2026-09-25)

**The defect class.** Upstream r82a's HTTP server treats the wire as
trustworthy in exactly the places where it is least: the request line
length, the header count, the header value length, the POST pair count,
the Basic-auth decode length, and the auth condition itself (which checked
`cfg.http.user[0]` twice and the password never — a pass-only config was a
wide-open interface). Every socket-facing protocol in this server got the
same lesson already (cacheguard 2.9 for CSP, the newcamd harnesses for the
client side); the HTTP parser was the last one still trusting the wire.

**Decision 1 — bounds at the destination, not the wire.** The parser does
not reject long requests; it stops writing at what the destination holds
(`buf2str` gained `max`; the header loop stops at 20; the POST pair loop at
19). The semantics stay stock for everything that fits — no new error
codes, no new rejections a legitimate client could hit.

**Decision 2 — the 340-char Basic refusal.** `base64_pdecode()` cannot be
given an output bound without changing a signature shared with the encoder,
so the call site refuses payloads longer than 340 chars before decoding:
64+64 credential bytes cannot base64 beyond that. One rule, no API change.

**Decision 3 — open only when NEITHER is configured.** Stock's open-when-
unconfigured behaviour is kept (an operator may run the interface without
auth on a trusted network — that is their decision to make, and stock made
it for them). What is not kept is the accident: a password configured and
the door still open. Half-configured now means Basic required, and the
credential is `:<pass>` in that case — documented here, not silently.

**Decision 4 — the tests must bite, and the rig must not lie.** The pre-fix
binary was rebuilt with the single old condition restored and answered
HTTP 200 with no credentials; the fixed binary answers 401. Every probe in
`ha` is followed by an authenticated 200 and a log sweep for backtraces —
"refused" is not enough, the server must also have survived.
(The session's own lesson, recorded with it: a leftover rig binary under a
DIFFERENT name — `/tmp/multics-pre31` — squatted the target port and made
the fixed build look broken; `pkill -x multics` does not match it. Process
cleanup must match every name it can meet.)

---

## D32 — TASK 3.2, names are data: escape at the consumer, not the builder (2026-09-25)

**The survey rewrote the plan.** The recorded defect was "cli->user
unescaped into HTML". The tree survey found upstream's own `xmlescape()`
already applied on exactly nine XML/ajax branches — and 40 HTML table-row
call sites, three detail pages, four XML `<name>` writes, the /debug log
printer and the debug-flag labels all raw. The first patch escaped inside
the cell BUILDERS; that would have double-escaped every XML path (builder
escape + branch escape). Reverted.

**Decision 1 — escape at the raw consumers, with upstream's own tool.**
The 40 HTML branches now run the same `xmlescape(cell[i])` loop the XML
branches always ran, right after building the cells; the direct write
sites (detail rows, `<name>` lines, /debug log printer, flag labels,
freeCCcam's inline cells) use the new bounded `html_esc()`. Builders stay
raw: every path escapes exactly once.

**Decision 2 — five metacharacters, byte-for-byte otherwise.** `html_esc`
rewrites only & < > " ' — an honest name renders identically to stock
(unit-pinned). Bounded, no allocation, truncates with a terminator rather
than writing past the buffer.

**Decision 3 — the /debug page prints log lines as text.** The log ring
(carried client names via debugf) went into a `<pre>` raw. Lines are now
escaped at print. The telnet DEBUG view stays plain text on purpose — no
markup there.

**Recorded, not fixed here (open defects):** `debugf` formats through
`vsprintf` into a 1024-byte line and `add_dbgline` `strcpy`s it into a
512-byte slot — a line longer than ~900 bytes (a >880-byte client name)
would overflow upstream. Protocol buffers bound names far below that in
practice; logging-core hardening is its own task.

**Verified live** (`make xs`, and the bite): the pre-fix binary renders
`<script>alert(1)</script>` raw on the detail page (1) and the debug page
(7 log lines); the fixed binary renders raw nowhere and escaped on both,
with login, index and logs unaffected. Bite prerequisites learned the hard
way and recorded: the old binary needs `stdbuf` for its log (block
buffering swallows everything a `kill -9`ed process never flushed), ports
must be clear of rigs under ANY name (`oldb`, `multics-pre31`, …), and
`pkill -f` self-matches its own command line — bracket-class the pattern
or match by short comm name.

---

## D33 — TASK 3.3, the logging ring: clamp at the slot, keep the semantics (2026-09-25)

**The defect, precisely.** `debugf()` vsprintf'd its formatted line into a
1024-byte stack frame, then `add_dbgline()` `strcpy`'d it into
`dbgline[..][512]` in .bss. Two unbounded copies back to back; the ring
slot was the smaller, so ~900 caller bytes (reachable wherever a name or
an evidence string grows) wrote past it. `fdebugf()` had the same
`vsprintf` into 4096 stack bytes; the format string itself was also
copied unbounded. Everything upstream logged — and everything this
project logs, the `!!!` evidence sentences included — funnels through
this one ring.

**Decision 1 — clamp at the destination, keep every line a line.** The
ring's unit of meaning is a line; a truncated line is still a line, and
the truncation point is the slot edge the readers already assume. Nothing
is dropped, nothing is split across slots, nothing past the slot is
written.

**Decision 2 — the semantics are upstream's, pinned by tests.** Append,
wrap at 70, cursor on the next slot, the /debug walk reading
(cursor-35)..cursor-1 forward: `dbg_store()` reproduces exactly that and
`test_debugcore` pins it, so the page and the telnet view see the same
ring as before — only bounded.

**Decision 3 — bounded formats too, not just lines.** The format string
comes from the caller as surely as the arguments do; both copies are now
bounded. `vsnprintf` is bounded by what the frame still holds after the
timestamp, with an explicit final terminator.

**The bite, live.** The widest legal username (62 chars; the newcamd wire
guard refuses >63, config stores it in 64 bytes) flows through five ring
lines and /debug renders all of them escaped with the name whole; a
900-char login attempt is refused at the connection layer; the ring, the
page and the server stay healthy. (The harness itself segfaults on
900-char argv before the server can even see the login — the server's
refusal stands either way, and the probe says which.)

**Recorded for the harness, not the server:** ncclient's login build
`strcpy`s the username into buf[512] — its own overflow, found while
wiring the probe. Harness-side, clamp before sending.

## D34 — TASK 3.4, the cache allocation: guard at birth, drop one event (2026-09-26)

**The defect, precisely.** `getcachetabbycaid()` allocated the per-CAID
cache-list node and its 1024-slot table (~88 KB) with no NULL checks;
`cache_new()` allocated `cache_data` nodes in three branches, no checks;
`cache_setdcw()` and the PIPE_CACHE_REPLY handler allocated
`cw_cache_data`, no checks — and every caller dereferenced the results.
The cache path is hot and it is the process: one failed allocation was a
SIGSEGV on the cache thread. Proven, not assumed: an LD_PRELOAD interposer
(`tests/ca-bite.c`) refusing exactly the table allocation — calloc(90112),
an 88-byte packed `cache_data` × 1024 slots, the exact form GCC turns the
malloc+memset pair into (read out of the shipped binary's disassembly) —
killed the 3.3 binary with exit 139 on the first honest client ECM, and
the same bite on the fixed binary survives with four bounded
`CACHE: out of memory` lines for two ECMs.

**Decision 1 — guard at birth, drop one event.** Fourteen guards, one per
allocation and one per caller that dereferences what an allocation
returned. On failure the caller drops the ONE event it was holding: a find
(the ECM keeps its own cachetimeout and falls back to the card servers), a
request to peers, a reply's remembered copy, one peer's agreement report.
No pool, no freelist, no struct or layout change — the reserved categories
stay untouched by choice, and GR9 holds: nothing blocks, nothing queues,
bounded memory only.

**Decision 2 — the table exists before it can be found.** In
`getcachetabbycaid()` the node is linked into `cachelist` only after its
table allocation succeeds, and the node's table field is written before
the link. A half-built node can never be found and dereferenced by a
later call — the failure mode is "no table for this CAID yet", never "a
table that lies".

**Decision 3 — say it once, bounded.** The only log added is one bounded
line per failed table attempt. The per-node failures are silent: they are
per-request events (the worst case is one line per ECM), and the table
line already names the CAID an operator needs.

**Recorded narrowing:** a `cache_setdcw()` report dropped for memory is
counted nowhere. Under memory pressure a source under-reports agreement;
it never invents it. That is the honest direction for a counter that
feeds proof thresholds.

**Tests:** units unchanged 26 groups / 1051 ok; new live target `ca`
(9/9 ×3): phase 1 bite-active survival + bounded lines + HTTP alive with
the table missing; phase 2 clean restart with a real cache peer — the CW
arrives byte-exact at a real client, so the guards never touched the
happy path. Full pass 14: rc=0, 251 ok. `make -C tests docrefs`: 410/0,
advisory back to the 3.3 baseline after re-deriving the 17 shifted
`clustredcache.c` references by content.

## D35 — TASK 3.5, the config USER record: bound at the field, warn once (2026-09-26)

**The defect, precisely.** The newcamd `USER` branch handed
`parse_str()` the struct fields themselves: `parse_str(usr->user)`
then `parse_str(usr->pass)`. `parse_str` clamps at 255; the fields are
64 bytes, adjacent, in a packed struct. Two faces, both silent:

- name >63: the spill writes the name's tail over `pass`'s head;
  `userhash` is then hashed over a string that crosses the field edge.
  Bitten live: the stored "name" of a 70-char entry became the first 63
  chars glued to the password bytes.
- pass >63: the spill writes straight over `userhash` (and `type`).
  Bitten live, and this is the sentence an operator feels:
  `USER: u4 P*70` answered `unknown user 'u4'` — to u4's OWN configured
  password. The record was destroyed at parse time; the control pair
  `u3 p3` on the next line logged in fine.

**Decision — bound at the field edge, warn once, keep the credential
usable.** Both copies are bounded at their own field edge with one
config-parse warning each (line and column named, the house format).
No new error path: an over-long entry is truncated to 63, and since the
newcamd wire guard has always refused names >63, nothing that could ever
log in is lost — the truncated pair is a WORKING credential, which the
warning tells the operator in so many words. The alternative (rejecting
the whole USER line) would turn a long-name typo into a silently missing
user — worse for the operator than an explicit truncation line.

**Scope, recorded not bundled.** The same branch is the only site fixed.
During verification two sibling facts were recorded for their own
follow-up tasks: (1) `parse_str()`/`parse_name()` write `str[len]` with
len up to 255 — into any `char str[255]` caller (read_config's own
scratch among ~40 sites) that is a one-byte off-by-one for a 255-char
token; (2) the other six USER/PASS parse branches in config.c (camd35,
cs378x, telnet, http, freecccam, cccam clients and the server USER
fields) feed identically-shaped `[64]` fields and carry the same class.
Both are parse-layer/cold-path; neither justifies widening this hunk.

**Tests:** live target `cu` (ports 16720/16721, 7/7 ×2): exactly two
warnings; control pair untouched; both long entries log in through their
truncated forms; zero `unknown user` lines — the pre-fix signature is
gone. Pre-fix bites on the unfixed binary are in REPORT-3.5-ar.md.
Units 26 groups / 1051 ok; full pass 15 rc=0 / 258 ok (27 targets).

## D36 — TASK 3.6, SENDPIPE: the arm dies with its waiter (2026-09-26)

**The defect, precisely.** `CACHE_FLAG_SENDPIPE` (0x02) arms a cache
entry's push path: set by the local FIND/REQUEST pipe handlers exactly
when a local ECM waits on the cache (`pcache->ecm`), read by every push
gate — `cache_setdcw()`'s forced push, both FIND scans,
`cache_fetch_goodcw2/3` — and by the `TYPE_REQUEST` auto-answer towards
peers. Nothing in the tree cleared it: no `flags &= ~` anywhere. One
FIND armed an entry for its whole alive time (recvtime+alivetime, 45 s
default). After the waiter's death every peer key pushed for that hash
was force-marked `DCW_SENT` and piped at a waiter that was not there,
and since every scan skips `DCW_SENT` nodes, that key is buried for
every future ECM of the hash; peer requests got silence. The
cache-heavy black screen this project exists to kill, upstream-made.

**Map correction (matters for anyone re-reading the older notes).** The
block at clustredcache.c 703-761 — including the `icwlist` self-answer
and the SENDPIPE sets at 727/745 — is a commented-out direct-call
variant of `pipe_cache_find`. Dead text. The live arm sites are exactly
three, all in `cache_pipe_recvmsg`: PIPE_CACHE_FIND new-entry,
PIPE_CACHE_FIND existing-entry, PIPE_CACHE_REQUEST. The live
`pipe_cache_find` (763+) only serializes to the pipe. Established by
preprocessing (`gcc -E` shows zero `icwlist`) before anything was
written.

**Decision — clear at the death, guard the arm, nothing else.** Three
hunks, search `TASK 3.6`:

1. `cache_clear_sendpipe(ecm)` (clustredcache.c): fetch by
   (tag,sid,caid,hash) under lockcache; clear only when
   `pcache->ecm==ecm`. The back-pointer guard is the whole semantics:
   if a newer waiter re-armed the entry, the older ECM's death must not
   disarm it.
2. The two death moments call it, with the death committed FIRST:
   `ecm_setdcwdata()` after the SUCCESS commit (setdcw thread, inside
   lockecm — lockecm-then-lockcache, the order the `cache_check_cw`
   walk already uses; audited: nothing acquires lockecm while holding
   lockcache) and `ecm_faileddcw()` after the FAILED commit (ecm
   thread, lock-free context it already owns; the clear takes lockcache
   alone, no nesting, no cycle).
3. The three arm sites refuse a waiter that is already SUCCESS/FAILED
   (`cache_sendpipe_armable`, read under lockcache). This closes the
   only real race — death before the FIND/REQUEST is drained from the
   pipe — by ordering: the FAILED/SUCCESS commit precedes the clear,
   and arm decisions are serialized against the clear by lockcache, so
   "clear ran, then arm" cannot happen with a dead waiter. With
   cachetimeout 0 (the default) that race window is real upstream
   timing, not theory.

Never cleared at WAITCACHE-to-WAIT: a WAIT waiter is still servable
(`ecm_setdcwdata` accepts any non-SUCCESS ECM), so the arm stays the
delivery path until SUCCESS or FAILED. That is the constraint "clear
only when the waiting ECM is answered or gone, never earlier", kept
literally. No timer, no sweep (the constraint forbade a timer).

**Recorded, not changed:** upstream's PIPE_CACHE_FIND_SUCCESS handler
checks caid/hash/sid but not `dcwstatus`, so an in-flight push racing a
FAILED commit can still flip the dead ECM to SUCCESS and push to its
clients — a pre-existing microseconds-wide window, orthogonal to the
arm; the `DCW_SENT`-per-cwdata semantics (a key pushed to a LIVE waiter
stays unservable to the NEXT waiter after a re-arm) is upstream's own
design and remains — this task removes only the death-driven burial.

**Tests:** live target `sp` (16730-16733, sp-peer.c): the peer records
the server's request, stays silent until the cache-only waiter is dead,
pushes one valid key, then asks for the hash six times. Pre-fix
(the bite, on the 3.5 binary): key buried — the same ECM from a second
client decode-failed; six requests, zero answers. Post-fix: 6/6
answered, second client served instantly with the exact pushed key.
Units 27 groups / 1051 ok / 0 FAIL; `make -C tests all` rc=0 / 283 ok
/ 0 outcome failures, 28 targets (first `sp` inclusion);
`mcs/docs/all36-run.log` is the kept run log. cachepref green — the
TRUSTED-CACHE-FIRST gate does not read SENDPIPE and stayed untouched.

## D37 — TASK 3.7, the parse clamp: the terminator stays inside (2026-09-26)

**The defect, precisely.** Six functions in parser.c — `parse_str`,
`parse_name`, `parse_value`, `parse_int`, `parse_hex`, `parse_bin` —
shared one writer: clamp the copy at 255, memcpy, then `str[len]=0`.
For a token of 255+ chars the terminator went to `str[255]` — one byte
past any `char str[255]` caller. The plan named two of them; verification
found six, and the fix covers all six in one hunk class (same line,
same one-char defect; the 1.10a→1.10c precedent).

**The bite, and why it needed ASan.** In a plain build the stray byte
usually lands on padding and the harm is invisible — that is exactly
why the defect survived upstream for years. The proof instrument is
the real server compiled with `-fsanitize=address
-fsanitize-recover=address` for the three TUs that own the parse
buffers (main.c, parser.c, config.c; rule `x64/multics-asan`, now a
permanent tool and the backbone of the live `ps` guard). One config —
`TRUSTED-CACHE-FIRST: <255 X>` (parse_boolean's own buffer), a
`SERVER` line with a 255-char name (parse_server_data), a profile
`USER` with a 255-char name (read_config's scratch) — produced eight
`WRITE of size 1` stack-buffer-overflow reports in a single startup,
each naming parser.c and the caller's frame. Zero reports after the
fix, same config, same instrument.

**Decision — clamp at the source, not at ~40 call sites.** One char
changed in six lines: `len=254`. The alternative (threading a max
argument through every caller) was rejected before as a rewrite of the
parse layer; the alternative of fixing only `parse_str`/`parse_name`
would have left `parse_value` (the workhorse behind parse_boolean and
the CACHE PEER branches), `parse_int`, `parse_hex` and `parse_bin`
writing the same stray byte. 254 keeps every 254-char token complete —
the clamp never moved for it — and truncates 255+ one char earlier,
silently, as such tokens always were truncated. No warning is emitted:
parse warnings name config lines and the six parse_* functions have no
line context; the value change is one char on a token no real config
uses.

**Recorded, not changed:** (1) `parse_quotes()` copies the quoted body
with `strcpy` — unbounded, needs a quote character in the line; `HTTP
TITLE` reaches it — its own follow-up task; (2) the six sibling
USER/PASS config branches of D35 stay theirs (TASK 3.8); (3) the ASan
build compiles only main/parser/config instrumented — widening it to
every TU is 4.1's business.

**Tests:** unit `test_parser` (28th group, 36 asserts): the packed
canary at offset 255 — the exact byte — for all six functions across
254/255/400-char and empty tokens; pre-fix 15 asserts failed, post-fix
36/36. Live `ps` (16740/16741): the ASan server boots the three-token
config (200, profile parsed, zero reports), the plain -O2 binary boots
it identically. Units 28 groups / 1087 ok / 0 FAIL; live suite 29
targets rc=0 / 0 outcome failures (`docs/all37-run.log`).

## D38 — the config-credential branches: one bounded-copy helper, field-edge truncation (TASK 3.8) — 2026-09-26

**Situation.** D35 recorded the parsers' off-by-one as its own fix (3.7)
and named the straight-into-field USER/PASS parses as sibling damage.
Inventory, BEFORE any code: eleven live sites over seven surfaces
(camd35 ~:1506, cs378x ~:1603, cccam F-lines ~:2625, mgcamd ~:2931,
telnet ~:1774/:1782, http ~:1836/:1844, freecccam ~:2901/:2909) plus the
two http USER reads inside upstream's commented-out ADMINUSER/ADMINPASS
block — dead text, left untouched. The plan had said six branches; the
pattern class was wider, as 1.10a→1.10c and 3.7 taught.

**Structural fact that made this dangerous.** In every one of those
structs `user[64]` and `pass[64]` are adjacent (packed): camd35
`camd35_client_data` :441, cccam `cc_client_data` :929, mgcamd, cs378x
`cs_client_data` :275, freecccam (followed by maxusers), and the telnet/
http inline config structs — where the next fields are the **owning
thread's pid/tid**. A 70-char name spilled into pass; a 70-char pass
wrote over userhash or over a live pid/tid.

**Decision.**
1. One helper, `cfgfield.h`: `cfg_store_field(dst, dstsize, tok, nbline,
   col, what)` — bounded copy at the FIELD edge, one house-format
   warning per over-long field (config line+column, "the truncated pair
   stays a working credential"). Header, not .c: the unit test includes
   the exact production code; debugf/getdbgflag/DBG_CONFIG come from the
   includer (config.c already has debug.h; the stub path is for tests).
2. Truncation, not rejection: an over-long configured credential keeps
   working, consistently, on every surface — same choice as 3.5.
3. `userhash` stays hashed over the STORED name (the 3.5 recipe's
   contract): the credential on the wire always matches the hash.
4. `parse_quotes()`'s unbounded strcpy (HTTP TITLE) is NOT folded in:
   one defect class per task, it stays a follow-up.

**Bite (live, pre-fix, 3.7 binary).** HTTP USER/PASS and TELNET
USER/PASS all 70 chars: http Basic auth → 401 for the truncated pair;
telnet → TELNET-AUTH-FAIL; zero warnings. The stored telnet name read
`B×64+Q×6` (pass spill bytes grafted onto the name). Post-fix: four
warnings, http 200, telnet prompt reached — all with the truncated
63-char pairs.

**Verification.** New unit `test_cfgfield` (29 groups, 25 asserts):
canary `user[64]/pass[64]/userhash/can` — 63 fits byte-identical with
zero warnings; 64 truncates to 63 with exactly one warning; 70 and 254
truncate; empty stores empty; userhash+canary byte-stable in all cases.
New live target `u` (u-probe.c: a real telnet client that greets, logs
in, and reports one verdict): four warnings, truncated pairs accepted
on both http (200) and telnet (prompt), config parses to the end,
server alive. Full suite 30 targets (first run whose `all:` line
actually contains `ps` — a 3.7 sed slippage had left it out of the
real target; fixed and re-run): rc=0, 284 ok, 0 outcome failures.

**Revert.** Hunks carrying `TASK 3.8` in config.c; delete cfgfield.h's
wiring (or the header itself). Nothing else changed; releases stock
`6375f218…`, stats `71f2a5c1…`.

## D39 — the web cell escaper is bounded at the cell edge (TASK 3.9) — 2026-09-26

**Situation.** Upstream's `xmlescape()` wrote its escape expansion into
`char exml[5000]` on the web thread's stack, walking `dest` with no
bound, then `strcpy()`'d the result back into the caller's buffer. The
plan (from BLACKSCREEN-era notes) called it a cold path; the inventory
kept it as a small task. The real audit found it sharper: EVERY call
site in httpserver.c passes a `char[N][2048]` cell row (8, 10, 11 and
12 rows — all 2048 wide, grep-verified), so there are TWO unbounded
writes: the frame walk (past 5000 if a cell's escaped form exceeds it)
and the copy-back (past 2048 into the caller's cell for anything
escaping beyond that).

**Decision.**
1. The frame is the CELL edge: `exml[2048]`, break when `dest` reaches
   `exml+2041` (room for one 6-char entity + NUL), one house warning on
   truncation, NUL always inside. Consequences: the copy-back can never
   write past any caller's cell (max 2047 chars + terminator = 2048
   exactly), the web thread's stack loses 3KB of latent overflow, and
   both overflow paths close with one edit. Honest cells keep
   upstream's bytes and entities verbatim — `&apos;` stays `&apos;`
   (3.2's `html_esc` was NOT reused: its `&#39;` would visibly change
   honest pages).
2. Proof technique, now a permanent tool: `make-x64/probe_xmlescape.c`
   links the REAL httpserver TU compiled with ASan and
   `-Dxmlescape=real_xmlescape` (and `-Dmain=multics_main` to donate
   main()), then calls the production function with a 2047-`&` cell.
   Pre-fix: stack-buffer-overflow WRITE of size 5 at
   httpserver.c:1307 (the `&amp;` entity memcpy). Post-fix: clean,
   2045 whole entities, exit 0. The live `xe` target builds and runs
   it first — the same guard pattern as `ps`.
3. The live target surfaced that the profiles page/div tail rendered
   cells RAW (the id/row XML branch escaped; the HTML tail never did —
   the one family 3.2's sweep missed). Closed here as 3.2 completion:
   the tail loop escapes its 11 cells. Page bytes change only for
   names containing `& < > " '`; every existing target stayed green.

**Recorded-not-changed.** The `/profiles?action=xml` summary branch
prints `<name>%s</name>` straight from `srv->name` — no escaping. It
is the operator's OWN config value consumed by external scripts (the
page's JS consumes the escaped cell branches), not a peer-controlled
string and not part of the cell rows; left as-is to keep one defect
class per task.

**Verification.** Probe as above; live `xe` (port 16760): profile
section `[ xe&38 ]` renders as `xe&amp;38` on /profiles, never raw; no
truncation warning for honest values; config parses to the end; server
alive. Full live suite 31 targets rc=0, 292 ok, 0 outcome failures
(all39-run.log); units 29 groups / 1112 ok. Releases stock `e877a782…`,
stats `bf2d976e…`; dev strips byte-identical to stock.

**Revert.** The two `TASK 3.9` hunks in httpserver.c; delete the probe
rule and probe source.

## D40 — `DCW STATS: ON` is a sub-word of the existing DCW handler (TASK 3.10) — 2026-09-26

**Situation.** `acceptDCW()` counts why a key was dropped (checksum,
null/half-null, three repeated bytes) and how many were accepted, but
only when `dcwstats_on` is set, and only in a build compiled with
`MCS_DCWSTATS`. The header has always said the switch would be the
config line `DCW STATS: ON`. Until this task the only way to set it was
from a test program. The counters themselves, and their semantics, were
already proven by `build/test_dcwstats`; what was missing was the
operator's hand on the switch. The readout (telnet, web) is 3.11 and
3.12, deliberately not bundled here.

**Decision.**
1. The line is a sub-word of the ONE existing top-level `DCW` handler
   (`DCW TIMEOUT` / `MAXFAILED` / `RETRY`), not a new branch earlier in
   that if/else chain. A second branch matches first and swallows every
   profile line that starts with `DCW`, leaving the profile on its
   defaults. That was not hypothetical: the first cut did it, and the
   `sp` target failed deterministically (its `DCW TIMEOUT: 1500` stayed
   3000; the first ECM no longer failed as designed; the second client
   died at 3003ms). STATS is handled before the "undefined profile"
   check, so the line is valid with no profile open yet, and every
   other sub-word falls through to the code that already owned it.
2. Same boolean rules as every other switch: `parse_boolean()` accepts
   1/0/ON/OFF/YES/NO and returns 0 for anything else, so a typo lands
   OFF and the log says OFF. `init_config` resets `dcwstats_on` to 0
   with the other defaults, so a reload of a file without the line
   stops the gathering instead of keeping the previous file's answer.
   The reload path is the config thread's inotify watch
   (`reread_config`), not a signal.
3. A stock build has no counters. It still parses the line — one config
   file is shared between the two releases — consumes the value, and
   logs one warning: `DCW STATS ignored - this build has no dcwstats
   counters`. No symbol from the stats flavour leaks into the stock
   binary (the include, the reset and the assignment are all under
   `MCS_DCWSTATS`).

**Verification.** Live target `ds` (port 16770), eight asserts covering
ON, the reload re-arm, the silent default, the typo, the stock warning,
and — the regression pin — the profile page still showing
`DCW TIMEOUT 1500ms`. The stats release is rebuilt on every `ds` run;
a present-but-stale binary once reported the swallowed timeout against
sources that no longer swallow it. Full live suite 32 targets, rc=0,
300 ok, 0 outcome failures (all310-run.log). Units 29 groups / 1112 ok.
`sp` green again. Releases stock `923a20a3…`, stats `389ce878…`; dev
strips byte-identical to stock.

**Revert.** The three `TASK 3.10` hunks in config.c; delete the `ds`
target.

## D41 — telnet reads and resets the DCW counters (TASK 3.11) — 2026-09-26

**Situation.** TASK 3.10 gave the operator `DCW STATS: ON`. The
counters and `dcwstats_reset()` have existed since the Phase-4
instrumentation, and the header already named the telnet command that
would call the reset. Nothing in `telnet.c` did. An operator gathering
numbers had no way to read them, and no way to zero them without
restarting.

**Decision.**
1. One new command, `dcwstats`, in the existing telnet if/else chain.
   No second word: print `dcwstats: ON` or `OFF`, then one
   `<reason> <count>` line per slot, names from `dcwstats_reason_name()`.
   That name list is the contract the web page (3.12) must reuse, so
   the two readouts cannot drift. Second word `reset`: call
   `dcwstats_reset()` and answer `dcwstats: counters reset`. Any other
   second word: a usage line. It does not reset. `help` lists the
   command on both builds.
2. A stock build does not include `dcwstats.h` and does not reference
   the symbol. The command still exists — one config, one command list
   — and answers once: `this build has no counters; use the stats
   release`. It does not say "reset" for a reset that cannot happen.
3. No new lock. The header already accepts a torn increment on these
   diagnostic counters. Reset from the telnet thread while an ECM
   thread increments is that same model, not a new one.

**Verification.** Live target `dt` (telnet 16782). A cache peer pushes
an all-zero control word; checksum holds and `isnullDCW` rejects, so
`null/half-null` is the slot that moves. The peer is killed before the
proof session, so a push in flight cannot refill the slot between reset
and the follow-up read. The session shows ON and a non-zero count, a
bad sub-word leaves that count, reset zeros every slot, help lists the
command. A second boot with no config line reads OFF and zeros. A stock
boot answers the polite line, help still lists the command, and the
config parses to the end. Full live suite 33 targets, rc=0, 308 ok, 0
outcome failures (all311-run.log). Units 29 groups / 1112 ok. Releases
stock `12f1da10…`, stats `d62bbf17…`; dev strips byte-identical to
stock; dcwstats symbols 0 / 2.

**Revert.** The `TASK 3.11` branch in telnet.c and the help line; delete
the `dt` target and `dt-probe.c`.

## D42 — the home page shows the same DCW counters as telnet (TASK 3.12) — 2026-09-26

**Situation.** TASK 3.10 armed the counters and TASK 3.11 made them
readable from telnet. The operator's usual window is the home page,
which already refreshes itself. A second spelling of the reason names,
or a raw interpolation of them, would either drift from telnet or
repeat the 3.2 hole the day a name contains `&`.

**Decision.**
1. The section lives in `http_send_index`, inside the block both the
   full page and `/?action=div` send. Names are
   `dcwstats_reason_name()`, passed through `html_esc` (3.2, `&#39;`,
   not `xmlescape`'s `&apos;`). Counts are `%lu`. The status word is
   ON or OFF, escaped the same way. Today's names have no special
   characters, so the page text matches telnet; the escape is there so
   a later name cannot break the page.
2. No web reset. Reset stays the telnet command. A GET that zeros
   counters would be a state change on a page the browser refreshes by
   itself.
3. A stock build does not include the header and does not reference
   the symbol. The section still exists and says, once, the sentence
   telnet already uses.

**Verification.** Live target `dw` (http 16790). An all-zero CW from a
cache peer moves `null/half-null`; the peer is killed; the page, the
refresh fragment and telnet agree on every slot, and the matched count
is not zero. The page is 200 and still has uptime and `</html>`. The
fragment has the table and is not a second document (it is the upstream
header-less refresh body; curl needs `--http0.9` to read it, the
browser's own request does not). No config line: page and telnet both
OFF and zero. Stock page renders, names the release, no counter table.
Full live suite 34 targets, rc=0, 316 ok, 0 outcome failures
(all312-run.log). Units 29 groups / 1112 ok. Releases stock
`9e030cd1…`, stats `82be1f56…`; dev strips byte-identical to stock;
dcwstats symbols 0 / 2.

**Revert.** The `TASK 3.12` block and include in httpserver.c; delete
the `dw` target.

## D43 — the −100 card score is a cap, not a lock (TASK 3.13) — 2026-09-26

**Situation.** `cardsids_update` (`main.c:1774`) refused every update
once `val` reached −100, including the success that resets a negative
score to 0. With `DCW MAXFAILED` set, `srvtab_arrange`
(`loadbalance.c:223`) skips a card at `val <= -maxfailedecm`, so a
frozen −100 stayed excluded until restart. The option is clamped 0–100
(`config.c:3819`); the default 0 leaves the gate open, so the black
screen is the case where the operator has armed it. The penalty sites
in `cli-*.c` are a separate question: a rejected CW still scores −1.

**Decision.** The floor holds, and it recovers the way the negative
branch already does above the floor: a success sets 0, further failures
do not pass −100. Not a slow climb to −90 (that would be a new policy,
and with `MAXFAILED` below 100 a score of −90 is still excluded). Not a
change to who is penalized. The +100 ceiling stays a lock; this task
does not touch it. One log line on reaching the floor, one on recovery.

**Verification.** Live target `fl` (http 16800). The probe links
production `main.c` and calls `cardsids_update`: 100 failures show
−100, 50 more stay there, one success returns 0. Floor line once per
provider, recovery once, extra failures quiet. Ceiling still 100 after
a failure. The running profile page shows `DCW MAXFAILED` 100. Full
live suite 35 targets, rc=0, 335 ok, 0 outcome failures
(all313-run.log). Units 29 groups / 1112 ok. Releases stock
`1787c9a9…`, stats `fb78ba27…`; dev strips byte-identical to stock;
dcwstats symbols 0 / 2.

**Revert.** The `TASK 3.13` block in `cardsids_update`; delete the `fl`
target and `probe_cardsids.c`.

## D44 — BAD-DCW is a fixed snapshot, not a walk of cfg.bad_dcw (TASK 3.14) — 2026-09-26

**Situation.** `BAD-DCW` is parsed at `config.c:2053` into
`cfg.bad_dcw`. The walk that would have rejected those keys was
commented out inside `acceptDCW`, and `dcwfilter.c` (the file that
still contains a walk) is not in OBJECTS. Reload frees the nodes. A
reader on the ECM path must not follow a pointer the config thread is
about to free, and must not allocate.

**Decision.** `dcw.c` keeps a 32-entry table. `dcw_badlist_commit`
(`config.c:6530`) copies the list after the swap, then
`dcw_badlist_publish` (`dcw.c:136`) replaces the table under a
sequence counter. `acceptDCW` calls `dcw_on_badlist` (`dcw.c:192`)
after the three r82a tests. An empty table is the default and is
silent. A torn read does not reject. Keys past 32 are not stored; one
log line names the overflow. Removing the line publishes an empty
table and logs `BAD-DCW: list cleared` once. `dcwfilter.c` stays
uncompiled: the decision lives in the file that is already linked.

**Verification.** Live target `bd` (http 16810). Listed key not
delivered, `bad-dcw-list` counted 1, log `1 entry enforced`. An
unlisted key delivered, counter stayed 0. Reload without the line
logged `list cleared`. A fresh server with no line delivered the key
and logged no `BAD-DCW`. Stock rejects the listed key too. Unit:
empty accepts, listed rejects, other passes, clear accepts, the 33rd
key is not enforced. Full live suite 36 targets, 343 ok, 0 outcome
failures (all314-run.log). Units 1119 ok. Releases stock
`15aae7a0…`, stats `04599154…`; stripped dev matches stock; dcwstats
symbols 0 / 2. docrefs 451/0.

**Revert.** The `dcw_on_badlist` call, the table, `dcw_badlist_commit`
and its two call sites (`config.c:6625`, `th-cfg.c:51`), and the `bd`
target.

## D45 — TASK 3.16, a failed ECM is not revived by an in-flight cache push (2026-09-27)

**The window D36 recorded and left.** `PIPE_CACHE_FIND_SUCCESS` matched
caid, hash and sid, then queued the key. It did not read `dcwstatus`.
A push already in the pipe, or queued while the ECM was still waiting,
could land after `ecm_faileddcw` had committed `FAILED` and told the
client decode-failed. `ecm_setdcwdata` then treated anything that was
not already `SUCCESS` as deliverable, and flipped the dead ECM.

**Decision.** Two checks, both cache-only. The handler
(`th-ecm.c:380`) does not call `ecm_setdcw_marked` when the status is
`FAILED`, and logs `cache: not reviving a failed ECM`. The setdcw
thread (`setdcw.c:619`) returns without flipping if the source is the
cache and the status is already `FAILED`. `WAIT` and `WAITCACHE` are
still served. A card-server reply is unchanged. No cache-layout change,
no lock-order change.

The race is made deterministic only in the test. `MCS_316_HOLD_MS`,
unset in production, sleeps inside `cache_setdcw` while `lockcache` is
held, after the arm check and before the pipe send, so the timeout can
commit `FAILED` first. Absent, it is one integer compare.

**Verification.** Live target `fs` (http 16840). Immediate push
delivered. Delayed push across the timeout: client got decode-failed,
linger saw no key, log had the refuse line, server sent no key to that
client and stayed up.

**Revert.** Both `FAILED` checks, the hold, and the `fs` target.

## D46 — TASK 3.17, a 1024-card list is not read one past its end (2026-09-27)

**Situation.** The inventory named `peer_acceptcard`. That function has
no callers. The live check is `peer_card_binarysearch`
(`clustredcache.c:955`), used at the four request sites. `cards` is
1024 slots. In the packed peer struct the next field is `nbcards`, so
`cards[1024]` is the count, not a card. The r82a tail assigned
`cards[xl = xm + 1]` with no index check. On the lists probed before
this task that read did not fire, but it was not impossible, and a
count above 1024 read past the array on the first step.

**Decision.** The search lives in `peer_cards.h` and is what the server
calls. An index outside `[0, min(nbcards, 1024))` is not evaluated. The
signed comparison is unchanged: a high CAID still matches the way r82a
matched it. Changing that would be a separate behavior change, and this
task does not make it. The unused linear scan stops at `nbcards`; a
full list with no match rejects instead of falling off the end and
accepting. Slots are copied out of the packed struct. No allocation,
no lock change, no cache-layout change.

**Verification.** Unit `test_peercard`, 20/20, including every slot of
a 1024-card list and a count of 1025 on a 1024-slot buffer. The same
test under ASan: 20/20, no overflow. Live target `pc` (http 16850):
the server stored 1024 cards, delivered the key for a listed card, and
did not ask the peer for an unlisted card. The server stayed up.

**Revert.** Restore the r82a search body, delete `peer_cards.h`, the
`pc` target, and `test_peercard`.

## D47 — TASK 3.18, restart does not stop threads by hand (2026-09-27)

**Situation.** Upstream left `//TODO:stop threads` at `main.c:2788`,
just before `done_config()` and the double `vfork`/`execvp`. The
inventory named `httpserver.c` and `pipe.c`. Those two only set
`prg.restart`. The TODO is in `main.c`. Seventeen loops already exit
when that flag is set, but many of them are blocked in `recv` or
`poll` and will not notice until the socket closes or the process
exits. `create_thread()` (`threads.c:51`) detaches every thread.
Several stored ids are overwritten (three threads share `tid_msg`).
Client threads keep the id on the stack and discard it. There is no
list that can be joined.

**Decision.** Do not add a join or a cancel. Process exit, after the
new process has been started, is what ends the threads that have not
already returned. That is the upstream behavior, and it is the one
that already comes back. A stopper is a threading-model change. A
cancel of a thread that holds a lock can hang the restart. A hang is
worse than the current exit. The comment at `main.c:2789` records
this so the TODO is not implemented later by accident. No allocation,
no lock change, no cache-layout change.

**Verification.** Live target `rs` (http 16860). The page answered.
After `/restart` the old process exited, the log said `Restarting...`
and `Stopped.`, and the new process bound the same page port. No
`bind port failed`. The stripped stock binary is byte-identical to
the 3.17 release (`8d4ab5c0…`).

**Revert.** Restore the TODO comment and delete the `rs` target.

## D48 — TASK 3.19, MIPSel, ARM and SH4 compile here; they are not run (2026-09-27)

**Situation.** The inventory listed MIPSel, SH4 and ARM as unstarted
because this image had only `x86_64-linux-gnu-gcc`. `src/Makefile`
names those platforms, but the compilers are absolute paths under
`/opt` (mipsel at `src/Makefile:44`, SH4 at `:68`, Raspberry Pi ARM
at `:74`, coolstream ARM at `:81`). None of those paths exist here.
`aarch64-linux-gnu-gcc` and `mips64-linux-gnu-gcc` are named on
`PATH` and were also absent. `SIG_HANDLER` in `main.c` prints an x64
`ucontext` and stays off. `ecmdata.h` already compares a control word
as two 32-bit loads when the compiler is not x86_64 or ppc64. That
branch was not changed.

**Decision.** Do not rewrite the `/opt` lines. An operator who still
has those old toolchains would lose them. Install the Debian cross
compilers that match the three named platforms, and compile with the
x64 flag set minus `-m64`, in `make-cross/`. The products are
`bin/multics-r82a-mipsel`, `bin/multics-r82a-arm` and
`bin/multics-r82a-sh4`. They are a compile proof. They were not
executed: no qemu is installed, and each binary wants its own dynamic
linker. The x64 stock and stats releases are not rebuilt.

**Verification.** `file` reports MIPS32, ARM EABI5 and Renesas SH.
md5 `1259e3d4edcf02638da9b8e935898c69`,
`8d07b543289864617f21791b925be8e5`,
`4b4872eccb3400bf2e5ac54602b9db09`. The four `/opt` compilers are
absent. x64 stock remains `8d4ab5c0…`.

**Revert.** Delete `make-cross/` and the three binaries in `bin/` and
`dist/`.

## D49 — TASK 4.1, memory checks are official; two classes are exceptions (2026-09-28)

**Situation.** There was no official ASan, UBSan, TSan or valgrind
target. The first run found two defects and two classes that are not
fixed here.

The defects, fixed, each with the failing run kept:

1. A blank config line sets `pos` to `currentline-1` and reads that
   byte (`config.c`, the trim after comment stripping). UBSan reported
   it on the blank line of a normal config (`docs/memcheck-4.1/asan-live-before.txt`).
   The trim now stops on an empty line and does not step before the
   first byte. A trailing backslash still continues the line.
2. Main slept 100 ms and then read `cfg.cardserver`. Under ASan the
   config thread had not finished, so a config that has a profile was
   refused as if it had none, and HTTP never came up. The config
   thread now releases a flag when the first `read_config` returns,
   and main acquires it before the check. This is not a new lock and
   not a change to how the ECM threads share state. A profile-less
   config still exits 1.

The exceptions, not fixed:

1. TSan on the short live session reports 12 data races in the
   existing shared state (`threads.c` detach, telnet, the protocol
   recv threads, and the HTTP page reading profile fields). The list
   is `docs/memcheck-4.1/tsan-races.txt`. Closing them needs a locking
   scheme across those threads. That is a threading-model change, and
   it is reserved. The startup race on `cfg.cardserver` is not in
   this list; that one is the flag above.
2. valgrind on a process ended by SIGTERM reports 3,072 bytes in 3
   blocks as definitely lost, all `dynbuf_init` in `gererClient`.
   Those pointers live on the handler thread's stack. The process is
   killed, not unwound, so the stacks are gone. The only return from
   `gererClient` frees the buffer. An idle run with no client reports
   0 definite leaks. The suppression is `make-x64/memcheck.supp` and
   matches that one stack. Any other definite leak still fails the
   target.
3. Alignment UBSan is off. Packed structs make it a flood, and those
   warnings are TASK 4.3.
4. `parse_quotes` (3.8b) is still an open unbounded copy. This suite
   does not send it a long title, so it did not appear. It is not
   closed by this decision.

**Verification.** `make -C make-x64 memcheck` exits 0. 29/29 unit
tests under ASan+UBSan, TSan and valgrind. Live HTTP `/` and
`/profiles` answer 200 under ASan and under valgrind, with no
confirmed error. Empty config still exits 1. Stock
`6917a11c1c1d66b4345c771e87e3f7d9`. Stats
`79e14043126a233465fee5a4a149b544`.

**Revert.** Restore the 100 ms sleep and the old trim. Delete
`memcheck.sh`, `memcheck.supp` and the `memcheck` make target.

## D50 — TASK 4.2, the soak ceilings are declared and the run stayed under them (2026-09-28)

**Situation.** Phase 4 asked for a live load of about 20 minutes:
an ECM storm, channel hops, peer pushes in every mode the harness
already speaks, and the rejection counters on. There was no `soak`
target.

**Decision.** The target is `make -C tests soak`. It is not part of
`make all`, because that suite must stay short. The ceilings are
written in `tests/soak.sh` before the run starts: 65536 kB of RSS,
and no more than 16384 kB of growth after the two-minute sample.
The server is the stats release, with `DCW STATS: ON`. Eight peers
run at once. The client hops service on one connection. A second
login for the same user is closed by the server for 60 seconds
while the slot still looks busy; the harness does not fight that,
it keeps the connection. No server source changes.

**Verification.** 1202 seconds. 382 ECMs, each a different service.
The process was alive at the end. The log has no segmentation,
SIGSEGV, backtrace, or abort. RSS was 1636 kB from the start through
minute 18, then 1868 kB. Both ceilings hold. After the peers were
stopped, telnet and the home page printed the same five counters:
accepted 3414, checksum 486, and three zeros. The counters were ON.
Stock and stats hashes are unchanged (`6917a11c…`, `79e14043…`).

## D51 — TASK 4.3, the packed-member warnings in the main.c area are fixed locally, not suppressed (2026-09-28)

**Situation.** The earlier census counted 42 `-Waddress-of-packed-member`
warnings tree-wide and called them inherent to `-fpack-struct`, which must
stay (raw network buffers are cast onto packed structs). TASK 4.3's
acceptance: zero such warnings in the main.c area, or a signed accepted-risk
decision. On today's gcc 14 the main.c compile unit (main.c plus the sources
it includes) carried 29 of them, plus 1 in `httpserver.c` and 12 in
`config.c`, `sha1.c`, `md5.c`, `aes.c`.

**Decision.** No suppression flag and no accepted-risk: the warnings were
fixed locally with one macro, `PACKED_MPTR(base, mtype, member)` in
`common.h`. It computes `(char*)base + offsetof(...)` — a form gcc does not
warn about — and yields a bit-identical address to `&base->member`, so no
struct layout, no threading model, and no calling convention changes. Three
site classes: 19 `create_thread(&X.tid, …)` sites (pthread_t is an opaque
handle whose address is only stored), 8 `pipe(prg.pipe.X)` array decays at
startup, and 3 `time/localtime(&sms->rawtime)` sites converted to by-value
copies (`sms->rawtime = time(NULL)`, `localtime(&(time_t){sms->rawtime})`).
The by-value forms were chosen for scalars because they remove the unaligned
pointer entirely rather than hide it. One adjacent same-class site outside
the area — `httpserver.c:2644` — got the same one-line by-value fix and is
counted here explicitly. Result: **0 warnings in the main.c area and in
httpserver.c; 12 remain tree-wide in config/sha1/md5/aes** — separate
translation units, outside the declared area, untouched.

**Incident recorded.** During editing, one line of `clustredcache.c`
(`TYPE_PINGREQ` peer loop, `peerip = peer;`) was corrupted and the build
broke. It was restored to the pristine r82a text (verified against the
infosat r82 source viewer), and the whole tree re-swept: all 20 translation
units parse with zero errors.

**Verification.** Full syntax sweep: 0 errors, and 0 warnings of this class
in the main.c unit and httpserver.c. `make release` and `make release-stats`
build clean; dcwstats symbol checks pass (0 / 2). `make test` 25/25. The
90-second soak passes on the rebuilt stats binary (counters match, RSS
1720 kB). New fingerprints: stock `2848850737c1e104507a0f992d9cd60b`,
stats `ae406bff16d84af78c8ec600eeeb920d`. The old fingerprints
(`6917a11c…`, `79e14043…`) stay recorded in the TASK 4.1/4.2 entries.

**Revert.** Delete the `PACKED_MPTR` block from `common.h` and revert the 30
site edits (19 thread, 8 pipe, 3 time) plus `httpserver.c:2644`; rebuild both
flavours.

## D52 — TASK 4.5, the freeze: what "done" pins down (2026-09-28)

**Situation.** Phase 4's last task: freeze the project and close it against
the DoD list in `PROJECT-PLAN-ar.md` §0.

**Decision.** The frozen deliverables are:
- `bin/multics-r82a-stock-x64` md5 `2848850737c1e104507a0f992d9cd60b`
- `bin/multics-r82a-stats-x64` md5 `ae406bff16d84af78c8ec600eeeb920d`
- `MANIFEST-md5` at the `mcs/` root, covering `src/`, the build wrapper and
  its tests, the live-test suite, `docs/`, and both binaries. It is the
  last file generated, so it pins everything including the close-out
  report; it cannot pin itself.
- `docs/INDEX-ar.md` as the document index; `STATUS.md` marked complete;
  `REPORT-4.5-ar.md` as the DoD checklist.

Verification re-run on the frozen source (not inherited from earlier
rounds): both releases rebuild to the exact fingerprints above;
`make test` 25/25; `make -C tests all` green **twice consecutively**
(rc=0, 370 `[ ok ]`, 0 `[FAIL]` each); `memcheck.sh` clean (ASan, TSan
with the D49 exception — 12 races this run, valgrind, empty-config and
live session); the official 1200 s soak PASS on the frozen stats binary
(382 hopped ECMs, RSS 1608→1884 kB, counters match). GR1–GR10 reviewed
against tree anchors (table in `REPORT-4.5-ar.md`); no new code was
added in phase 4.3–4.5 except the bit-identical `PACKED_MPTR` sites, so
the ground rules that the phase-1/2/3 layers were built under are
reviewed, not re-proven.

Known-open, deliberately outside the freeze: package أ's `parse_quotes`
unbounded `strcpy` (3.8b) — never in scope of any delivered task; it is
the first item of any future round. Workspace-restoration flakes found
and guarded during this task (test harness only): exec bits on restored
binaries (`chmod` guards added to `u` and `soak`), the `ncclient` HTTP
poll now waits for the newcamd port before firing the client, and the
target keeps its logs on failure.

**Rollback.** Not applicable — a freeze is a record, not a behaviour
change. The tree remains buildable and testable exactly as before.

## D53 — TASK 3.8b, the one reopening: `parse_quotes` bounded by its caller (2026-09-28)

**Situation.** D52 recorded package أ's `parse_quotes` unbounded `strcpy`
as the one deliberately-open defect outside the freeze. The user ordered
one more round; the freeze was reopened for exactly this defect and is
closed again with it.

**Decision.** `parse_quotes()` gained a destination-size parameter
(`int size`) and truncates the copy at `size-1`, NUL-terminated — the
same rule as the 3.7 caps on `parse_str`/`parse_hex`/`parse_bin`. The
token SCAN is untouched: `iparser` still walks to the closing quote
wherever it is, and still stops just after it, so a truncated value never
shifts how the rest of the line parses. All 13 call sites pass their
real destination (`sizeof` of the target field): 9 × `read_config`'s
stack `char str[255]`, 2 × `http_file_data`'s malloc'd
`url[512]/mime[512]`, 2 × twin `serial.device[256]` and
`twin.chninfo.fname[256]`. The old `str[63] = 0` blemish after the twin
read (config.c:4657) is now dead weight but harmless; left as-is to keep
the diff minimal.

**Why silent truncation here while 3.5/3.8 warn.** Those fixes warn in
`config.c` because the parser sits inside `read_config` with the line
number and the key name in hand. `parse_quotes` lives in `parser.c`
beside `parse_str`/`parse_hex`, which have no line context at all — the
3.7 caps are silent for exactly this reason, and this fix follows the
same rule. The comparison is silent-but-alive versus silent-but-dead.

**Verification.** Pre-fix, both levels proven failing: an ASan probe on
the real `parser.c` wrote 401 bytes into a 255-byte buffer ("WRITE of
size 401"), and the real ASan server binary died with a
stack-buffer-overflow at `parse_quotes parser.c:231` from
`read_config config.c:2017` on a 400-char `HTTP TITLE` — no output,
process gone. Post-fix: `probe-pq` 7/7 (stack 255 and heap 512
truncation, fitting values byte-identical, empty/unterminated behaviour
unchanged), `test_parser` 49/49 with 14 new assertions, the live `pq`
target green on the plain binary (parses to the end, serves the
truncated title), `make -C tests all` green twice consecutively (373 ok,
0 fail each), `memcheck.sh` clean, and the official 1200 s soak PASS on
the new stats binary (382 hopped ECMs, RSS 1816 kB, counters match).
New fingerprints: stock `3d1f58b385a7a0eea720c3c59c3bdd29`, stats
`f6d29d2e8585f08537b6b645d4df6666`. `MANIFEST-md5` regenerated over
everything. Harness hardening kept from this round: exec-bit guards on
restored test binaries (now on every scenario target in
`stability.mk`), the `ncclient` target waits for the newcamd port, and
failure runs keep their logs.

**Rollback.** Revert `parser.h`'s declaration, `parser.c`'s body, and
the 13 `config.c` sites (each is a one-argument change); rebuild both
flavours. The probe and `pq` target can stay; they would then fail, which
is the point of keeping them.

## D54 — the post-freeze roadmap: user-sanctioned direction (2026-09-28)

The user asked for the project to keep pace with modern sharing
threats and to improve the design (discussion only, no code). Four
decisions were made explicitly by the user: (1) "design" means both
the internal architecture and the web UI, internal first; (2) the
first task at the next go-ahead is modern CacheEx hardening — our own
opt-in per-peer equivalents of the r107-era BLOCK_FAKE_CW / LOCAL_ONLY
/ CWCHECK / MAXHOP_LG semantics, each verified against this tree
(cacheex.c, cacheguard, trustagg) before adoption, never blind-merged;
(3) structural changes (queues/threads) are sanctioned but only behind
a compile flag, with the frozen D53 fingerprints untouched and still
the release truth; (4) the peer network is modern (r107/oscam), so all
new protections default OFF per peer to preserve interop with older
stock peers. Working document: `docs/ROADMAP-ar.md` (R1..R7 with fixed
acceptance gates). Grounding facts verified live this session: CacheEx
modes 1/2/3 and full cs378x already present; cache UDP path has no
handshake/rate-limit; HTTP/telnet logins have no throttling; profile
port counter leaks across `[` blocks (config.c:941-942) — kept for
compat with an explicit log warning, never silently broken; missing
HTTP PORT opens an unauthenticated web UI on 5500. Execution stays
one task per explicit go-ahead, docs + Arabic report included in
"done" for every task.

## D55 — TASK R1, the exchange-gate semantics (2026-09-28)

The four r107-era CACHEEX switches the user's config carried
(BLOCK_FAKE_CW / LOCAL_ONLY / CWCHECK / MAXHOP_LG) are NOT documented in
the official r107 reference (fetched whole; it documents only MAXHOP and
VALIDECMTIME) -- they belong to the extended mcsql lineage. Per D54's
verification rule the IDEAS are adopted with semantics this tree can
prove, never the patches: BLOCK_FAKE_CW forces the two deterministic
r82a content tests (checksum, repeat) on exchange-family arrivals even
when DCWFILTER is off; LOCAL_ONLY drops unsolicited exchange pushes
BEFORE the cache entry is created and is therefore global by nature (a
push with a waiting local ECM is solicited by definition; profile-level
lines are recorded but announce that the gate is global); CWCHECK=N
raises a confirmation floor on the existing same-CW arrival counter,
effective floor = max(N, CACHE THRESHOLD), capped at 5, with the stock
DCW_TIMEOUT fallback untouched; MAXHOP_LG is refused (undocumented; the
enforced cap stays CACHEEX MAXHOP) but the parser now prints a notice
instead of staying silent. Scope: the TCP exchange family only
(cccam/camd35/cs378x cacheex, both directions); CSP UDP is excluded on
purpose -- it has FILTER/THRESHOLD/cacheguard. Enforcement lives in one
choke point, cache_setdcw(); profile flags tighten the global gates
where a pending profile is known. Grounding discoveries, verified live:
getcsbycaprovid() (main.c:1878) requires the push's provid to be in the
profile's PROVIDERS list, so a profile without PROVIDERS silently
rejects ALL cacheex pushes at that door (badcw++); with the default
CACHE FILTER ON the store path returns before the serving pipe unless a
cycle is proven (the cm rig already ran CACHE FILTER: OFF for the same
reason). New releases: stats `7cf58f2e646d28c0d5d181b14c6aeeb6`, stock
`3d6e970c628bf89746e88b7887a86d0f`; the D53 frozen pair is preserved
byte-exact in `bin-frozen-D53/`. Live proof: the `ce` target drives a
real cccam cacheex mode-3 peer (cxpeer.c, full handshake) through
baseline delivery, all three refusals with exact dcwstats counts, and
floor-crossing delivery.

## D56 — TASK R2, the counted cycle refusal at the live site (2026-09-29)

**The decision.** The D9 amendment (final) forbids reopening `checkcycle()`
and ordered R2 to be written against the LIVE rule `cwcy_expect()` as a new
decision — this is that decision. R2 does not add a refusal. It **measures
the refusal that stock r82a already makes**, and gives the operator a switch
over the *accounting* of it, never over the refusal itself.

**The discovery that shaped it.** Stock r82a already refuses contradictory
declared marks **silently at cache ingest** — `cache_setdcw()`'s «Check
Cycle» block, clustredcache.c:1777:
`if ((pcache->cwcycle!=NO_CYCLE)&&(pcache->cwcycle!=cwcycle)) { status|=DCW_ERROR; return; }`.
The entry's expectation `pcache->cwcycle` is PRE-STORED by the ECM FIND
itself: `put_ecm2cache()` (clustredcache.c:753-754) encodes exactly
`cwcy_expect(ecm->ecm[0], ecm->cw1cycle)` into the UDP find, the
PIPE_CACHE_FIND handler (:2555+) writes it on the entry, and the push path
only carries keys that passed ingest. A delivery-time gate in
`ecm_setdcwdata()` was implemented first, proven **UNREACHABLE** for
contradictions (the push path carries only ingest-survivors, and the
stored-CW scan requires cycle equality), and removed. The ingest block at
:1777 is the ONLY live refusal site — the unmeasured enforcement D9 spoke
of. Stock refuses wrong-half AND unmarked keys there; both stay uncounted
under it.

**What was built.** Inside that stock block, untouched, a counting arm:
when `cwcy_judge(pcache->cwcycle, cwcy_observed(cwcycle))==CWCY_CONTRA`
(both halves declared and different — the same verdict TASK 2.5's
observation records one storey up) and the profile arms `DCWFILTER CYCLE`
(per-profile tristate over the global default OFF, exactly like TASK
3.15's two gates), the refusal is counted `DCW_REJ_CYCLE` ("cycle-
contradiction" in telnet/web dcwstats) and logged with its reason:
`[CYCLE GATE] wrong-half key refused at ingest (expected X, saw Y) ch …`.
An UNMARKED key refused under an expectation stays uncounted (GR3: silence
judged as silence, not as contradiction). Disarmed (default) the block is
byte-identical stock: silent refusal, no counter, no line.

**Scope lines.** No `checkcycle()` code revived (the amendment stands); no
new refusal invented — the armed mode only makes an existing refusal
visible; the counter rides MCS_DCWSTATS like every other reason; the
`DCW_REJ_COUNT` grew 8→9 (test_dcwstats updated: array, names).

**Grounding, verified live.** `cy` target (tests/stability.mk): real
cachepeer with a 30-byte marked reply (fwd=1 required for the buf[29]
parse, clustredcache.c:1986). Baseline (gate unset): the wrong-half key is
refused by STOCK (client decode-failed), no CYCLE GATE line,
`cycle-contradiction 0`. Armed: same refusal, one CYCLE GATE line with the
reason, `cycle-contradiction 1`; then an agreeing key (other half, mark
matching) is delivered and the counter does NOT move. Rig facts worth
keeping: cache requests fire only after CACHE TIMEOUT (2000 < DCW TIMEOUT
4000), a second cache peer on a fresh port is advertised within ~9 s while
a re-bound port rides the 59 s backoff, and the agreeing phase must use a
fresh channel AND a fresh newcamd user (the old slot keeps stale ecm
state).

**New releases.** stats `bd2618ba708256f1433b38de03e81436`, stock
`bf158e72cd783f562fad0c7788714f78` — the stock build carries the gate code
and the DCWFILTER parse (default OFF = stock behavior) but no counters,
same split as R1. The R1 pair (stats `7cf58f2e646d28c0d5d181b14c6aeeb6`,
stock `3d6e970c628bf89746e88b7887a86d0f`) is historical from today. The D53
frozen pair stays byte-exact in `bin-frozen-D53/`.

**Tests.** `make cy` green (8/8 assertions); unit suites 31/31 with
test_dcwstats 19/19 (cycle name + count 9); full `make -C tests all` exit
0; official 1200 s soak on the new stats binary PASS (see the R2 report).

## D57 — TASK R3, the login doors: progressive delay + optional allowlist (2026-09-29)

**The decision.** Both operator doors (HTTP Basic, telnet login) answered a
wrong password instantly and forever — the R-round surface audit named this
as the standing hole: no attempt lockout, no delay, no list. R3 closes it
OPT-IN, like every protection in this project, with two global per-door
options (the doors are global by nature; no profile scoping exists):

- `HTTP LOGIN DELAY` / `TELNET LOGIN DELAY` — the base in ms, **default 0
  = off = stock exactly**. The n-th CONSECUTIVE failed login from one
  source address waits `base<<(n-1)` ms, capped at 8 s, before the stock
  failure answer goes out. A successful login resets that address's
  count (an admin's typo costs one base delay, never more). A slot quiet
  10 minutes starts from zero.
- `HTTP LOGIN ALLOW` / `TELNET LOGIN ALLOW` — optional, default empty =
  off. Non-empty: a connection from an address not on the list is closed
  BEFORE any credential is read, one `[LOGIN ALLOW]` line with the
  address, nothing else. Exact addresses only (max 16) — this tree has no
  mask parser and inventing one was a new attack surface for no operator
  need; hosts can be listed individually.

**Why sleep is safe here.** Both servers run one thread per accepted
connection (httpserver.c gererClient, telnet.c telnetprocess), so the
delay burns the attacker's own thread, never a shared loop; the accept
loops keep polling. This is why the remedy is a delay at the failure
answer and not a lockout: a lockout table would need the same state and
add a denial lever (an attacker who knows the victim's IP could lock the
victim out); a delay only slows the attacker himself.

**Semantics pinned.** Failure = wrong user OR wrong password (the answer
text is stock, unchanged — the throttle must not leak which one matched).
Counting is per source address across BOTH failure kinds, shared table
with the delay (loginthrottle.c, 64 fixed slots + one mutex, house shape
of purge/rotation). Every counted delay prints one
`[LOGIN THROTTLE] <door>: failure #n from <ip>, delaying <x>ms` line; the
off state prints nothing and answers instantly — byte-identical stock.
The only stock-visible change when off is internal: telnet's per-
connection parameter grew the source address (it never carried it; the
accept loop had it all along).

**New releases.** stats `937c4f1bb819a346c7b058bb3d1c647a`, stock
`27276ad904ff476c429229d4479de077` — both flavours carry the code (the
parse and the doors), the delivered cfg keeps every line commented. The
R2 pair is historical from today; D53's frozen pair stays byte-exact.

**Tests.** Unit: `test_loginthrottle` 25/25 (pure schedule: base/x2/cap/
overflow; allowlist: empty-passes/listed/stranger/full-list; table:
independence per address, success reset, quiet-slot expiry via an
injected clock — the module is built with -DMCS_LT_NOGLOBALS and the
test supplies lt_now). Live: the `au` target — phase 1 pins the stock
floor (3 wrong telnet logins in 4-5 ms total, instant 401s, empty log);
phase 2 makes the same guesses pay (250/500/1000 ms measured at the
wire, the right password instant and resetting, the http door delayed
too, one throttle line with the reason); phase 3 proves the list both
ways (listed address logs in and opens the page; the stranger's
connection closes pre-auth with one log line, and the compliant
server's log stays clean). 1200 s soak PASS on the new stats binary.

## D58 — TASK R4, the persistent peer-reputation ladder: confirmed events, explained escalation (2026-09-29)

**The decision.** R1/R2 taught the cache to REFUSE poison at two gates
(LOCAL_ONLY/BLOCK_FAKE_CW/CWCHECK and the counted DCWFILTER CYCLE refusal),
but a peer that keeps sending poison suffers nothing lasting: it is refused
peer-instance by peer-instance, forever free to try again. R4 adds the
memory — a per-peer ladder, `monitor -> distrust -> isolate -> ban`, fed
ONLY by confirmed, individually-logged refusal events (today the two R2
classes: cycle-contradiction and cacheex-fake-cw), shipped as
`peerrep.{c,h}` and OFF by default like every protection in this project.

**The keys.** `PEER REPUTATION: ON` (default OFF — zero accounting, zero
files, every gate passes, byte-identical stock); `PEER REPUTATION FILE:`
(default `multics.peers` next to the cfg); `PEER REPUTATION DISTRUST:` /
`ISOLATE:` / `BAN:` — thresholds in confirmed events, shipped 5/20/50.
The ladder only climbs. Nothing auto-forgives: no timers, no decay, no
restart amnesty. The file is the source of truth — an operator who decides
a peer has earned another chance edits the file (deletes the line, or
lowers the stage) and restarts; that is the documented recovery path, and
the rig proves it end-to-end (phase 5). Every escalation prints one line
with the reason and the consequence:
`[PEER REP] peer <host>:<port> ESCALATED to <stage> -- last event '<reason>' -- consequence: <what>`,
and the file is rewritten atomically (tmp + rename) at every escalation.

**The consequences are the two levers R1/R2 already built.** distrust
(>= 5 events): the ask gate — this peer's keys are no longer requested
(the find fan-out skips it); isolate (>= 20): the door gate — its pushes
are refused before ingest, BUT the arrivals still count: a peer that keeps
pushing while isolated is climbing its own ladder, the one thing the
pusher fully controls; ban (>= 50): the stock `FLAG_DISABLE` lever — the
peer is disabled, the pings stop, and the ledger freezes (drops past
FLAG_DISABLE are stock-silent, so the file keeps the last counted state).
A restart reloads the file and re-arms the ban the moment the peer shows
up, said once (`ban restored from the reputation file`). At most 64 slots;
`telnet PEERREP` prints the table with stage/events/last-reason.

**The semantics pinned.** Events are CONFIRMED refusals only — the same
bar R2 set (measured contradiction, not suspicion); unverified or stale
keys never touch the ledger. Identity is `ip:port` as the cache already
sees it. The gate placement is deliberate: the ask gate sits in
`cache_send_request` (the single fan-out), the door gate after
`getpeerbyaddr`/IS_DISABLED in TYPE_REPLY (so a disabled peer is dropped
by stock first, and the file never counts what stock already stopped).

**Two traps found live and fixed (both ours, caught by the rig).** The
config branch read the option letter from the string AFTER parse_int had
overwritten it with the value — thresholds silently stayed at defaults
while every line parsed cleanly; the option letter is now captured before
the value is read, and the applied thresholds are parse-logged
(`PEER REPUTATION thresholds: distrust=… isolate=… ban=…`). And the
escalation used to log before saving — the rig raced the log against the
file rename; the file is now pinned BEFORE the log line says so.

**A stock finding recorded, not fixed (out of scope).** With DCWFILTER
CYCLE armed, a refused wrong-half reply poisons the cache entry for that
ECM: an honest peer's agreeing key arriving after it is not delivered
(the client times out) even though nothing is wrong with the honest key.
It is stock multi-reply interplay (DCW_ERROR on the entry), visible only
under R4's confirmed-refusal flood; R4's rig choreographs around it (the
honest peer serves alone first; in the recovery phase the distrusted
peer's replies are inert short frames so the honest path is the only
candidate). A candidate task for a future round, requiring its own
decision entry.

**New releases.** stock `7ef8001aec52a5edfded3867fe7e0d08`, stats
`20e89edd9447540c0a14300208e93554`; the dev binary
`make-x64/x64/multics` `c1090981d935325a57a64d6fe06abae2`. Both flavours
carry the code; `PEER REP` strings 8/8; dcwstats symbol gate 0/2 held.

**Tests.** Unit `test_peerrep` 33/33 (stages and thresholds on
`peerrep_note`, file round-trip incl. atomic rewrite, load-merge,
clamp ordering, off-state pass-through, 64-slot cap, reason truncation
canary). Live target `pr` (ports 16920-16925), five phases, 14 asserts,
~110 s: baseline delivery through the lone honest peer; the flood climbs
the ledger — third event distrusts, the file carries the record, a fresh
channel is asked to A only (A 3+, B 1); events 4-5 still land (distrust
listens), the sixth isolates, the pushes are refused at the door and
STILL count, the tenth bans — pings stop, the ledger freezes at 10; the
restart re-arms the ban from the file once and the mesh still serves; the
operator deletes the line, restarts, and the mesh is whole again with the
recovered peer receiving finds. Full `make -C tests all` exit 0 (one
pass, ~25 min). 1200 s soak on the new stats binary PASS (1202 s, 382
ECMs, max RSS 1892 kB).

## D59 — TASK R5, the machine readout: /json behind the same door (2026-09-29)

**The decision.** Every number this project has spent four rounds making
countable lived behind an HTML page or a telnet command — readable by a
person, useless to a machine. R5 adds ONE route, `GET /json`: a single
JSON document carrying the six sections an operator's Grafana or alert
script needs — `version`, `uptime`, `now`, `dcwstats` (every named
counter, `accepted` first), `peerrep` (the R4 ladder: on-flag, the three
thresholds, every record with stage as number AND name plus events and
the last reason) and `cache_peers` (the rows the /cache page shows, each
merged with its ladder record as `rep_stage`/`rep_stage_name`/
`rep_events`/`rep_last`). Hand-rolled writer, no external library —
`src/monjson.{c,h}` is a PURE builder: the caller snapshots the live data
(the same unlocked read the stock HTML pages have always done), the
module only renders, and a document that would overflow its buffer is
abandoned whole, never served truncated.

**The doors it sits behind.** The route is inside gererClient()'s chain
AFTER the Basic-auth gate every page passes, so /json inherits exactly
the door the operator already runs: same credentials (or the open door
when neither HTTP USER nor HTTP PASS is set — TASK 3.1 semantics), the
R3 allowlist closing pre-auth, the R3 delay taxing guessers. No new
config key: /json is one more read-only page on the same surface, not a
new listening socket. The stock (non-stats) flavour serves the identical
document shape with `dcwstats.on = 0` and zero counters, so a dashboard
built against one flavour works against both.

**Semantics pinned.** Key order is fixed (the tests pin it); counters
are integers; the reputation stage is BOTH a number (for alert
thresholds) and the same word the ladder logs, telnet PEERREP prints and
the reputation file stores. The per-channel trust matrix (trust.h, the
per-bouquet scoring) stays internal ON PURPOSE: it is a scoring
intermediate, not an operator alert surface — the peer-facing trust
state an operator acts on is the ladder + the peer rows, and that is
what the document carries. Escaping: quote/backslash/control bytes per
RFC 8259, bytes >= 0x80 pass through raw (UTF-8); peer names are data,
not markup (htmlesc's lesson, applied to a second format).

**One trap found live by the unit test before it ever served a byte.**
The output helper took a length argument and three call sites passed the
wrong one — `mjput(o,"},",1)` shipped `}` and ate the comma, and the
whole document was invalid JSON with every key adjacent. The helper lost
its length parameter (strlen inside), and the unit test now pins comma
discipline for the empty, single and multi-row shapes. Second, smaller:
the tree builds with -fpack-struct, so the thresholds are copied through
locals — taking the address of a packed member is fatal there.

**New releases.** stock `394559fb634515ed42e3ac33ac6202f8`, stats
`f304622469748a478e5ce5cbe41cea1d`, dev `ffba2abba5c281507eaa0833d9cf0ced`.

**Tests.** Unit `test_monjson` 26/26 (escaping set incl. \u00XX and
exact-fit; minimal-document shape with the full counter name set;
comma discipline; the ip:port record format and row merge; name
escaping; overflow → -1 with an untouched canary and a byte-exact cap;
a 256-peer document inside canaries). Live target `jm` (ports
16940-16945), 8 phases, ~62 s: the stranger's 401; the six sections
parse; every counter name as an integer (a stats build shows the flood
in `cycle-contradiction` too); the armed 3/6/10 thresholds with an
empty list; the honest online peer as a row with a ping and
`rep_stage: -1`; a REAL 150 ms poisoning flood escalating (the log line
checked before the document is read); the live ladder record in the
document; the same record merged into the peer's row. Full
`make -C tests all` exit 0 in one pass (~26 min, 429 ok). 1200 s soak
on the new stats binary PASS (1203 s, 382 ECMs, max RSS 1964 kB).

## D60 — TASK R6, the bounded cache queue behind CACHE_QUEUE (2026-09-30)

**The decision.** R6 separates the cache socket's RECEIVE path from the
cache PARSER with a bounded SPSC ring — behind the compile flag
`CACHE_QUEUE`, default OFF, zero new config keys. With the flag off the
source compiles out completely: the default dev binary keeps fingerprint
`ffba2abba5c281507eaa0833d9cf0ced` (verified ×8 this round) and the stats
release rebuilt BYTE-IDENTICAL to R5 (`f304622469748a478e5ce5cbe41cea1d`)
— the strongest possible proof the stock path is untouched.

**Design v2 (final, after one measured regression).** The receive thread
stays the PRODUCER for peer datagrams only: recvfrom → wire-range check
(2..512 bytes) → `cq_push` into the ring, zero locks, and it keeps its
stock 3-second wakeup duties. The pipe fan-out stays EXACTLY stock and
inline (lock + `cache_pipe_recvmsg` on pipe-ready) — moving the pipe
drain into the worker was built, measured as a regression, and reverted.
The worker is the CONSUMER: an unchanged `cache_recvmsg` reading its
source through the `cq_active`/`cq_current` statics, polling the ring at
a FLAT 10 ms idle (`usleep(10000)`).

**The cadence is measured law, not taste.** 500 µs wakeup → 3810 ms CPU
against the stock 430 ms on the same load (≈9×) and a 12% steady spin
with ONE idle peer (99 ticks/8 s). Adaptive 200/1000 µs → WORSE (6700 ms).
Flat 10 ms → 550 ms CPU, idle 3 ticks/8 s (idle-class). The worker's
worst-case added latency, 10 ms, is 200× smaller than CACHE TIMEOUT
(2000 ms) and 40× smaller than DCW TIMEOUT (400 ms). Decision recorded
in-source; do not retune below 10 ms without a new measurement.

**The double A/B (make pm, raw peers, 100 ECM):** default delivered
100/100, cpu 420 ms, rss 1708 kB; queue delivered 100/100, cpu 550 ms,
rss 1856 kB, queue_dropped 0 — Δ ≈ +130 ms CPU per ~6500 datagrams
≈ 20 µs/datagram, wall parity. First real queue exercise: enqueued 6426,
dropped 0, end-to-end decodes. The ring: 256 × 512 B, drops the NEWEST
when full and counts it; the worker prints one tally per 60 s and only
if work happened since startup — silence means idle, never stuck.

**The suite went flavour-aware.** The wing's ds/dt/dw targets asserted
STOCK-only behaviour (the inert-option warning, the no-counters telnet
answer, the counter-less page) — against a queue binary they now detect
the flavour via `strings $(BIN)` (nm is useless on stripped releases) and
print a `[ -- ] flavor check` note instead of failing. Full wing on the
queue-stats binary: **exit 0, 425 ok, 0 FAIL, 3 flavor checks.** jm on
queue-stats: 8/8 ×3, zero zombies.

**Suite hardening recorded for the next round:** (1) a `#` comment line
with a tab inside a backslash-continued recipe joins the shell line and
swallows everything after it — it silently orphaned `$$ok`/`$$srv` and
left zombie servers; keep explanations ABOVE the recipe. (2) `pkill -x
multics` misses renamed binaries (comm = `multics-r82a-qu`) — use
`pkill -9 '^multics'`. (3) jm's hop loop must walk DISTINCT declared
sids 0x64–0x71 (the decimal base is 100 — printf %02X), must break on
B's OWN "got request" (the flood peer seeds its push identity from its
first find only), and needs the 8-second settle before phase 8 reads the
three piled contradiction events.

**The make-test audit closed:** 34 prerequisites, 33 run lines.
`test_loginthrottle` (25/25) now runs in sequence; `test_peerrep` stays
a documented BUILD-ONLY prerequisite — it segfaults when run right after
test_monjson (in-sequence state pollution; standalone it exits 0) and is
covered end-to-end by jm's live-ladder phases instead.

**Releases:** queue `c08556f9a4591048d107b0f8fc173dd9`, queue-stats
`e1607001b338e9517366964255e2bdce` (both carry the tally string and the
15 cq_ symbols). MANIFEST reopened for R6: 327 entries, self-pinned
(the pin is the last line of MANIFEST-md5 itself: md5 of the file minus that line). The delivered `/home/user/multics.x64`
remains the R5 stats build, byte-identical, HTTP smoke 200.

## D61 — TASK R7, the new skin: self-contained pages, Arabic, token sessions (2026-09-30)

**The decision.** The web UI becomes fully self-contained and bilingual.
The skin is ONE internal stylesheet served INLINE in every page head —
no second request, no CDN, no external font or script — so an air-gapped
box, a LAN without internet, and the sandboxed preview all render the
exact same page. The legacy class hooks every generator already emits
(.menu/.maintable/.infotable/.option/.alt1-3/.online/.offline/...) are
all kept, so the 23 replyok / 22 html / 21 style write sites only changed
their WRITER, not their markup. Three shared writers replace the stock
static writes — `http_replyok_write()` (carries the Set-Cookie block),
`http_html_write()` (emits `<html dir=rtl lang=ar>` for Arabic), and
`http_style_write()` (inlines the skin, or keeps the `<link>` when the
operator's FILE STYLESHEET override is configured — the stock override
still wins). Any NEW page must use the writers; the old static writes
cannot carry cookies or direction. The viewport meta is in every head;
at ≤760 px the tables become swipeable blocks and the brand caption
hides. The partial-AJAX autorefresh (the #mainDiv swap) is stock
behaviour and is kept as-is.

**Arabic is a real mode, not a coat of paint.** `?lang=ar` (or the
MCSLANG cookie it sets) turns on `<html dir=rtl lang=ar>`, Arabic menu
labels (الرئيسية/التنقيح/الخيوط/الخوادم/الكاش/الباقات/نيوكامد/مجكامد/
المحرر/إعادة تشغيل/خروج) and the RTL layout rules; `?lang=en` flips
straight back. The boundary is deliberate: menu, captions and chrome are
bilingual; table DATA stays technical (peer names, CAIDs, counters) —
translating live data serves nobody.

**Token sessions ride the SAME door.** One successful Basic login mints
a 256-bit token (/dev/urandom) returned as `MCSSESSION` (HttpOnly,
SameSite=Strict, Max-Age=1800); later requests may present the token
instead of credentials. The doors do NOT move: the R3 allowlist still
closes pre-auth before any of this runs, and a request with no valid
cookie and no credentials still walks the R3 progressive delay into the
same 401. Sessions are bound to the source IP, idle-expire after 30
minutes, die absolutely after 12 hours, live in a 16-slot RAM table
(oldest evicted; a RESTART logs everyone out — deliberate: no session
file to steal). Minting happens ONLY where Basic actually matched.
`/logout` needs no credentials BY DESIGN — it can only invalidate the
token the caller already holds — and expires the cookie on the way out.

**The stock INFOSAT forum link is gone** — the skin keeps pages free of
any external reference, anchors included.

**Lessons recorded this round:** (1) config keys NEST: the stylesheet
key is `FILE STYLESHEET: "path"` — a bare `STYLESHEET:` is silently
ignored by the parser (no error, empty value). (2) Grep assertions on
rendered pages must anchor to the literal tag (`<HTML dir=rtl`), not a
substring the CSS itself contains (`html[dir=rtl]` in the stylesheet
made a false positive). (3) A killed server's log is empty unless the
run used `stdbuf -o0` — stdout to a file is block-buffered and `kill -9`
discards it. (4) `head -c N` counts bytes INCLUDING trailing spaces —
a prefix compare off by one byte fails three runs before you see it.

**Releases (all flavours carry the skin; fingerprints moved as one):**
stock `300d3373511b54fb573c8740699b5e55`, stats
`7ad6e449a7f05e2602ef7d626e7484ed` (= the delivered `/home/user/multics.x64`,
HTTP smoke 200), queue `1c62e782a1ffe4b8c2ae35441e2bf1c8`, queue-stats
`6b47ba4ab0eb7889e1803a5fb3fbbaf9`, dev `2ac2cbb1b44ec94f33743cb28d37870e`.
**Proof:** full wing on the new dev build exit 0 (439 ok, 0 FAIL) —
including the R3 gate tests (delay/allowlist/401) unchanged — and the new
wing target `sk` 10/10 ×3 on the dev build AND on the delivered stats
binary: token mint, cookie-only access, forged-token 401, stranger 401,
logout expiry, Arabic rtl + persistence, English flip, /style.css builtin,
dead-token /json 401, FILE STYLESHEET override link. Units: 1257 ok,
`make test` exit 0.

## D62 — TASK R8, the cache consensus arbitrer: CACHE CONSENSUS (2026-09-30)

**The decision.** The one question a structurally clean key could never
answer — WHO VOUCHES FOR IT? — is now asked, behind `CACHE CONSENSUS`
(default OFF, stock byte-for-byte when off, zero new threads). The R6
queue separated the path; R8 adds the judgement inside it: one tally per
(channel, ecmd5, cw) counts DISTINCT voters — a flood of re-pushes from
one peer is still one vote, closing the hole where one eager liar
defeated stock CACHE THRESHOLD's arrival counter. The first key of a
request is HELD for a corroboration window (CACHE CONSENSUS WINDOW,
default 400 ms, clamped 100..2000): a second DISTINCT voter delivers
early (unanimous exit — multi-peer channels pay almost nothing), a lone
key is released by the window walk on the existing housekeeping tick
(a single-source channel loses only the window, never the key; card
servers are never touched by the hold). On contradiction the WEIGHTED
decision rules: a voter already distrusted on the R4 ladder weighs zero,
so reputation — not arrival order — picks the key; the weighted loser's
node dies, the refusal is counted as the TENTH dcwstats reason
(`consensus-mismatch`) and the loser is convicted on the ladder. Losing
on TIMING alone convicts NOBODY — an honest-but-slow peer must not be
punished for arriving second; the earlier key still delivers, so the
fake wins at most the cold tie, once, and never after the ladder warms.

**The division of labour is explicit.** Consensus arbitrates ONE request;
the R4 ladder is the cross-request memory; the R2 cycle gate keeps its
ingest refusals; the R1 exchange gates keep their doors. The residual is
documented, not hidden: a structurally perfect fake that wins the cold
timing tie delivers ONCE per channel-relationship; every weighted loss
after that flips the window against it.

**Hold semantics reuse stock shapes** — the hold returns the exact
CWCHECK skip (stored, undelivered, retried), so no waiter, timer or
thread was added; the release walk rides the peer-check sweep in both
the epoll and poll receive loops (release cadence = window + one tick).

**The live proof (wing target `cn`, 4 phases ×3):** OFF delivers exactly
as stock and says nothing; ON holds a lone key and releases it at the
window (availability); two honest voters corroborate and deliver BEFORE
the window; with a delayed fake in the room the clean key delivers and
the fake never lands. Unit `test_cwconsensus` 25/25 (weights, ties,
sticky conviction, expiry, channel/tag isolation, fail-open). Full wing
green; jm 8/8 ×2 (dcwstats grew the tenth named counter — jm asserts the
full list). **Fingerprints (all flavours carry the engine):** dev
`08a46f59275471e5b843132ec3f923df`, stock `8cb1db156138e6f541bb5950db4e3ac8`,
stats `59ea3fd1f0aad698b741a20f1b572c6b` (= the delivered
`/home/user/multics.x64`, HTTP smoke 200, `sk` 10/10 on it), queue
`54cb4abc8042e9f1107349a5d3e3e24e`, queue-stats
`c35548a0aa32ce5de96a89225e4ea011`.

**Suite lessons this round:** (1) `test -x` on the release binaries
fails after any rebuild leaves them 0644 — chmod +x is part of building,
again. (2) A probe added to a pure module needs the debugf declaration
the tree's own headers cannot give it (debug.h pulls in the packed-struct
world) — declare the one function locally, then REMOVE the probe. (3) A
wing phase that reuses one newcamd user across quick reconnects trips
the slot cleanup; give each phase its own user.

## D63 — TASK R9, the test_peerrep "sequential crash" was a bare fopen on a
## missing directory; the test rejoins make test (2026-10-01)

**Situation.** Since R6 the unit was a build-only prerequisite: a Makefile
note claimed it "segfaults when run in sequence right after test_monjson
(in-process state pollution)" and coverage was delegated to the jm live
ladder. Every round since paid the tax: "peerrep build-only".

**Root cause (verified with ASan).** Separate processes cannot share
memory — the folklore was impossible on its face. ASan pinned the crash
to the test's own fixture writer: with `/tmp/pr-unit` absent (fresh
machine, cleaned /tmp), `fopen("/tmp/pr-unit/rep","w")` returned NULL
and the first `fprintf` died inside `fwrite` on the NULL stream. The
test "passed standalone" in earlier rounds only because a stale
`/tmp/pr-unit` directory lingered from an older session. test_monjson
was never involved; nothing runs before main in that binary.

**Decision.** The test now mkdirs `/tmp/pr-unit` (EEXIST is fine) and
writes every fixture through `xopen()`, which fails loudly instead of
crashing. No src/ change — peerrep.c itself was and is safe (loading a
missing file yields 0 records, already asserted). The run line rejoins
`make test` immediately after `test_monjson` — the exact sequence the
folklore blamed — as the standing regression. Release fingerprints are
untouched; only the Makefile and test_peerrep.c move.

**Verification.** 33/33 in all three scenarios (fresh /tmp alone, right
after test_monjson, repeated runs); `make test` exit 0, **1315 ok over
30 groups** (1282 + 33, exactly the re-admitted unit). The single
"FAIL" string in the log is a check NAME ("a pure mirror FAILS the
group sums"), not a failure.

## D64 — TASK R10, the cross builds and the bundles meet the current tree
## (2026-10-01)

**Situation.** The arm/mipsel/sh4 binaries dated from TASK 3.19 (2026-09-27):
make-cross's hardcoded SRCS was still the 20 stock files, so the three
boards had no login throttle (R3), no peer ladder (R4), no /json (R5),
no skin (R7) and no consensus engine (R8). Both bundles were D53-vintage
(the ready cfg introduced itself with "D53, 2026-09-28").

**Decision.** (1) make-cross SRCS grew by exactly the four modules the
x64 OBJECTS line added since (loginthrottle, peerrep, cwconsensus,
monjson) — same flags, stock flavour (no MCS_DCWSTATS, no CACHE_QUEUE),
matching 3.19's best-effort stance. (2) All three rebuilt; verification:
file(1) arch check, string-parity with the x64 reference (4x "[CONSENSUS]"
lines, MCSLANG skin, /json, PEERREP), and — first in this project's
history — a LIVE execution: the arm binary under qemu-arm with its
sysroot answers HTTP 200. Cross toolchains and qemu are session packages;
they do not survive the workspace snapshot and must be reinstalled for
any future cross round. (3) The ready bundle was refreshed in place (same
member list): current stats+stock x64 pair, the cfg gained a commented
opt-in block for the post-D53 switches (zero behavior change — every key
still OFF), current docs, current manifest. (4) multics-project.zip was
redefined instead of guess-refreshed: it is now "the manifest made
tangible" — the 329 pinned files that can legally live inside it (the
three bundle archives cannot contain themselves) plus an in-zip
MANIFEST-md5, making the zip self-verifying: extraction + md5sum -c =
329/329 OK. The old 476-file zip had no membership rule and no in-zip
verification. (5) The manifest header's stale "bundles cannot be pinned"
line was corrected — they are pinned entries like everything else; only
the manifest itself and a zip containing itself are unpinnable.

**Verification.** Cross: 3/3 built clean, arch-correct, string parity,
arm live HTTP 200. Bundles: extraction -> ./start-multics.sh start -> UP,
web 200 with the cfg's own credentials, telnet dcwstats ON, clean stop,
port closed after. project zip: 329/329 OK in the extraction. Fingerprints
(cross now): arm `800177a56d2f4e6f3a0a0ce5dac71c95`, mipsel
`dfe5a832b822aee1c2487ee5871c0ae9`, sh4 `0ec90fd16489b58f55bd50e804b4c27c`
(pre-R10: 8d07b543…, 1259e3d4…, 4b4872ec… -> historical). Bundles: ready
tar.gz `74b44a4b691603c3f0182b4af3ab4b55`, ready zip
`a3f608e95e55440c5627f209c3bac31a`, project zip
`f14f4c97d6dba7d31da631d95fc95b0b`.

## D65 -- TASK R11, the per-CW verdict ring: /cwlog page + /json section
## (2026-10-01)

**Situation.** The counters answer "how many keys were refused and why";
the operator's actual question kept being "WHICH key, from WHOM, on WHICH
channel, and what happened to it". The debug log answers in lines that
scroll away.

**Decision.** `src/cwlog.{c,h}`: a fixed 100-entry ring (one mutex, zero
allocation, drop-oldest) of verdict records -- tick, caid:provid:sid,
verdict (delivered/refused/held/stored), reason (the DCW_REJ_* numbers
0..9 plus ring-only 10..13: held-for-corroboration, convicted-memory,
below-threshold, profile-filter), raw peer id, and the 16 key bytes
themselves. Fed ONLY from the stats-flavoured cache verdict sites in
clustredcache.c (the five named gates, the consensus hold/refuse, the
convicted gate, the threshold skip, the profile-structural refusal, the
delivery funnel pipe_cache2ecm_find_success, and a STORED hook at the
ingest exit for no-waiter seeds, guarded by a `piped` flag so nothing is
recorded twice), and ONLY while `dcwstats_on` is set -- the ring lives
and dies with the counters' own switch: **no new config key**.
The page `/cwlog` (menu entry "CW Log"/"سجل الأحكام", newest first,
colour-coded verdicts, R7 shell rules: three writers, embedded style,
mlabel, same authenticated chain) and a `cwlog` section in /json
(shape-identical in every flavour, on=0 with empty rows in stock, name
arrays always present) are the two readouts. `cwlog.o` links ONLY where
the counters link: the release recipes now pass an `MCS_DCWSTATS=1` make
variable alongside the -D flag (the CACHE_QUEUE=1 pattern). Flavour
fingerprints move because monjson/httpserver shared code grew; the frozen
D53 pair stays byte-preserved; OFF behaviour parity is the suites' job as
always.

**Verification.** `test_cwlog` 37/37 (round-trip, newest-first,
drop-oldest wraparound at the capacity, guards, and -- the header's own
claim -- reason names 0..9 word-for-word the dcwstats names, pinned
against dcwstats_reason_name in the unit). Live target `vl` (ports
16830-16833), 4 phases: held-then-delivered rows with the key bytes on
page and /json; the cn-P4 fake as refused/consensus-mismatch; a 400-find
fresh-key storm filling the ring to 100/100 with the server alive; the
stock binary not serving /cwlog at all. Full wing + make test in the
round log.

**Suite lessons this round.** (1) A one-line format edit lost to the
fuzzy matcher left `+lumins%lus`: 11 conversions for 12 varargs, `%x`
reading a `long`, and a `%s` dereferencing an integer -- the page died
DETERMINISTICALLY mid-write at exactly 5799 bytes, 299 buffered bytes
lost, an empty table under honest counters. ASan passed it (register
luck), gdb passed it (timing); the byte-exact truncation plus a manual
format audit caught it. A truncated response is a crash scene. Grep-verify
every edit -- third strike for the silent-fuzzy family. (2) The /json
rows array was block-scoped with its pointer escaping into the input
struct: a textbook use-after-scope that -O2 punished and -O1 hid; moved
to function scope. (3) cachepeer's CP_FRESH_KEY answers FINDS, it does
not self-push; the fresh keys are checksum-broken by design (group 4), so
a fresh-key storm needs `DCWFILTER CHECKSUM: OFF` and the storm client
needs `timeout` -- a hung ncclient hangs the wing. (4) The server ignores
UNDECLARED cache peers entirely; every test peer must be in the config
(the undeclared fake produced nothing, twice). (5) no-waiter seeds never
reach the delivery funnel (no SENDPIPE): the STORED verdict had to be
hooked at the ingest exit behind a piped flag.

## D66 -- TASK R12, the stock reply-interference defect: a refused
## declaration must not convict the key bytes (2026-10-01)

**Situation.** The queue held one item, parked "outside scope, candidate
for a coming round with an independent decision" by R4 (D58): under an
armed `DCWFILTER CYCLE`, the refused (wrong-half) reply poisons the ECM
entry and the later HONEST key from an honest peer is never delivered --
the client starves on a zero-DCW timeout. Invisible: the swallow happens
before any gate, counter or log line.

**Root cause (reproduced live before touching anything, on the R11
binaries, evidence in docs/interference-R12/).** cache_setdcw() stores one
cw_cache_data node per KEY BYTE SET. The stock r82a cycle refusal --
"declared mark contradicts the entry's expectation" -- marked THAT node
`DCW_ERROR` and returned. Two peers can carry the very same bytes with
different marks (the R4 scene made B do it on purpose; any real mesh can
do it by accident): the second reply of the same bytes, the honest one,
hits the status early-out -- `if (cwdata->status&DCW_ERROR) return` --
and dies silently. The node carried a PER-REPLY verdict as if it were a
PER-BYTES verdict. Confirmed live: exactly one cycle-gate refusal (B),
then A's mark matching the expectation swallowed, counters unchanged,
client timed out.

**Decision.** The cycle contradiction is a verdict about a DECLARATION
against an expectation -- evidence about the peer, never about the
bytes. The node poison is gone: the refusal stays (this reply is still
not delivered, the counter/peerrep event still fire), a repeat with the
same wrong mark is refused again per reply, and an honest mark on the
same bytes now flows through and delivers. Content-based refusals
(half-nulled non-NDS) and the consensus convictions (the R8 loser-kill
and consensus REFUSE) KEEP their node marks: those judge the bytes
themselves. This reverses D9/R2's "the refusal is stock r82a and stays
untouched" for the node mark only -- the independent decision the queue
demanded. No flag: a defect fix, not a new protection; all flavours
move together and the OFF-parity doctrine compares wings to stock AS IT
NOW IS.

**Verification.** New live target `ri` (ports 16955-16960, ~10 s, two
phases): P1 = the trap itself (same key on both peers, B mark 2 answers
first and is refused, A mark 1 answers +400 ms and MUST deliver); P2 =
fresh key on a fresh channel round-trips (refusals are per-reply, never
per-key). Green on the stats release AND the plain dev binary. The
R11 ring narrates the trap in two rows: refused (B) then delivered (A)
on the same bytes. Units untouched: make test still 1352 ok. Full wing
449 ok (447 + ri's 2). Scene lessons folded into the target: cachepeer
peers must be up BEFORE the first find (a peer started mid-target
missed its ping window and never joined); the ncclient harness must
log in as a DISTINCT user per phase (rapid re-login of the same user
died in the newcamd handshake); one shared key across sids triggers the
GR3 reuse proof (same cw on a different service) which convicts the key
globally -- correct protection behaviour, so each phase now carries its
own checksum-valid key on its own channel; and every client find is
retried once before its phase can fail.

## D67 -- TASK R13, a refused push must not testify: the cwreuse sighting
## guard, plus the hygiene that the failed target was hiding (2026-10-07)

**Situation.** The wing had exactly one red target (`ce`) and it had been
red for three identical runs. The forensics (isolation runs A/B/C under
`docs/evidence-R13/`) pinned the trigger: an unsolicited 0x00C8 push that
`LOCAL_ONLY` refuses, followed by an honest push of the SAME control word
on the good service (0x0064). In the failing run the log showed the
refusal first and then `CW NEGATIVE MARK ... 1884:000000:0064 ...
proven unable to open its picture`, `CW REUSE PROOF`, `CACHE PURGE` and
a trust demotion within two seconds. The mechanism: every caller did
`res = cache_setdcw(...)` and called `cwreuse_offer(...)` UNCONDITIONALLY.
`cache_setdcw` returns negative for the three PRE-STORAGE refusals
(LOCAL_ONLY, the profile gate, BLOCK_FAKE_CW), so a refused push still
created the FIRST SIGHTING of the key; the later honest delivery of the
same bytes was then judged a reuse proof, the good key was filed in
negative memory, `cwn_gate` refused every later store, the peer threshold
was never crossed and the ECM was never served. This is not a test
artifact -- it is a pre-poisoning attack: a peer that knows a key can push
it for a service nobody wants, have it refused, and watch the honest
delivery of those bytes be convicted globally.

**Decision.** Only a key that ENTERED the cache may testify. Guard all six
call sites with `res >= 0`: `src/srv-cccam.c:998`, `src/srv-camd35.c:319`,
`src/srv-cs378x.c:452`, `src/cli-cccam.c:521`, `src/cli-camd35.c:259`,
`src/cli-cs378x.c:329`. The refusal itself is untouched (still dropped,
still counted, still logged) -- what changed is that the refusal leaves no
memory. No new flag: a defect fix, all flavours move together.

**Also fixed, in the same round (same reason: they were the real warnings
the build was drowning in).** `httpserver.c`: `#include <fcntl.h>` for the
`/dev/urandom` open and a `struct http_request;` forward declaration (the
type was declared inside a parameter list). `debug.c:202`:
`va_start(args, fstr)` -> `va_start(args, format)` (undefined behaviour;
x86-64 tolerated it, the cross targets are where it bites). `cwlog.h`:
the ring API (`cwlog_note/count/capacity/snapshot/reset`) had NO
prototypes -- callers in `httpserver.c` and `clustredcache.c` were implicit
declarations. `make-x64/test_cachequeue.c` and `make-x64/test_cwlog.c`
were MISSING from the tree while `make test` claimed 36 binaries and 1352
checks: recreated, 23/23 and 37/37, and the cwlog test pins the contract
that ring reasons 0-9 spell exactly what `dcwstats_reason_name()` spells.
`tests/stability.mk`: the three `ce` scenarios used to overwrite each
other's logs (now `.ce-s{1,2,3}.log`, `.ce-nc{1,2}.out`); `kill_srv` used
`pkill -9 -x multics`, which can never match anything because the process
name is truncated at 15 chars (`multics-r82a-st`) -- now an anchored
`pkill -9 -f "^$(CE_BIN) -C $(CE_CFG) -v$"`; `CE_KEEP=1` keeps the
evidence on a green run and prints a `[note]`, not a `[FAIL]`. A first
attempt at the pkill pattern (`-f "[m]ultics-r82a-stats-x64 -C .ce.cfg"`)
killed the recipe's own shell and make reported `Killed`: a `pkill -f`
pattern must never appear in the recipe text it is running.

**Verification.** `make ce` exit 0, 3 scenarios, 10 ok, 0 fail
(`docs/evidence-R13/ce-post-R13.out`); the kept per-scenario logs show the
refusal WITHOUT any negative mark, purge or trust demotion
(`.ce-s2.log`), against the pre-fix log quoted above. `make -C make-x64
test` exit 0, 1352 ok. Full wing re-run: see `REPORT-R13-ar.md` §2 (the
wing number is counted from `make -k all`, not from a hand-picked subset).
All four x64 flavours rebuilt; fingerprints stock `88a3bf29...`, stats
`3c2c3df4...`, queue `c344b06e...`, queue-stats `72d3a3dc...`, dev
`6eb62ab3...`; the shipped binaries are proven to be their declared
flavours by their strings (`[CACHE QUEUE] enqueued`, `DCW STATS`) because
`strip` removes the symbols the in-Makefile self-checks use.

**Not closed, and now recorded rather than implied.** The cross binaries
`bin/multics-r82a-{arm,mipsel,sh4}` predate R13 and no toolchain for them
exists in this environment (checked). The 12 `-Waddress-of-packed-member`
warnings stay, deliberately documented with file:line. `src/cw1cycle`,
`multics.log`, the unsafe `strcpy/strcat/sprintf` population and the
missing CI are enumerated with priorities in `docs/REPORT-R13-ar.md`
(O1-O27, M1-M30). `.gitignore` added (build outputs and rig scratch; the
real `tests/multics-*.cfg` files are verified not ignored).
**Addendum 5, same round (R13/D67) -- the memory-check path was dead twice
over.** `make -C make-x64 memcheck` is the path R4's decision cites as
"exits 0". It could not run at all: the five scripts the rigs invoke
(`memcheck.sh`, `soak.sh`, `docrefs.sh`, `autelnet.py`, `cedcw.py`) are
tracked as mode 100644, so `./memcheck.sh` answered "Permission denied"
(Error 127) from a clean checkout -- and `tests/Makefile:34` already had a
`chmod +x soak.sh` patch with the telling comment "a restored workspace can
lose the exec bit", i.e. the disease was known and treated one target at a
time. The bits are fixed in the index now (100755). Underneath that,
`memcheck.sh` carried its own copy of the link's source list and had gone
stale -- it stopped at `main.c`, so the ASan/TSan/valgrind live server could
not link once `loginthrottle.o`, `peerrep.o`, `cwconsensus.o` and
`monjson.o` joined the build (`undefined reference to peerrep_save ...`).
There is now one source of truth: a `print-srcs` target in `make-x64/Makefile`
derives the list from the same `OBJECTS` the release links, and `memcheck.sh`
asks for it. Measured on a copy of the tree (the R4 logs stay untouched):
**ASan+UBSan 36/36 and TSan 36/36 on the units, the live server clean under
ASan (`races=0`) and carrying the eleven already-recorded TSan races that
D49 treats as a decision rather than a failure.** Only the valgrind leg
cannot run here -- the tool is not installed (`env: 'valgrind': No such file
or directory`, rc=127) -- so R4's "exits 0" claim is verified for the
sanitizers and left explicitly unverified for valgrind (M40).

**Addendum 4, same round (R13/D67) -- one byte on the telnet port, and the
noise that hid the warnings.** The HTTP fix in addendum 2 sent the sweep
looking for the same shape elsewhere, and `src/telnet.c` had it three times:
`recv()` then `buf[len-2]`/`buf[len-1]` with no guard for `len==1`, so a
single byte (before any credential is read) made the login path read
`buf[-1]`, and had that byte been 0x0d it would have written `buf[-2] = 0`.
A `len<2` refusal now sits next to the existing `len<=0` at all three sites,
and a new live target `tl1` (stability.mk:4933-4974, wired into `all`) sends
exactly one byte, asserts the server survives with a clean log, then logs in
properly to prove the console was not muted (4/4,
`docs/evidence-R13/tl1-post-R13.out`). The same walk found two unchecked
allocations in the accept paths (`httpserver.c:8008`, `telnet.c:522`) --
`malloc()` followed by `->` with no test; both now close the connection on
failure. Separately, the build was answering two C++-only options
(`-fpermissive`, `-Wno-return-mismatch`) in a C build and printing 48 cc1
lines per flavour over the 12 real warnings -- noise four times the signal,
which is how a warning stops being read. The options are gone from
`make-x64/Makefile` (the same cleanup in `make-cross` is M37, unverifiable
here without a cross toolchain). That also made the unit build's own twelve
warnings visible, and a line-by-line pass found exactly one real item in
them -- `peerrep.c:37 stage_name()` is dead code, marked and listed for
deletion -- plus two deliberate truncation tests worth keeping and four
cosmetic false positives now written clean. Final state: every flavour exits
0 with exactly 12 warnings, all `-Waddress-of-packed-member`; `make test`
1352 ok, 2 intended warnings; wing 52 targets.

**Addendum 3, same round (R13/D67) -- cleanup could not see its own server.**
Fifteen recipes in `tests/stability.mk` ended a server with
`pkill -9 -x multics`. Linux truncates the process name to 15 characters, so
the shipped flavours appear as `multics-r82a-st` / `multics-r82a-qu` and the
exact match killed nothing -- proved live: with the server answering 200,
`pkill -9 -x multics` returned 1 and the process survived, while
`pkill -9 '^multics'` returned 0 and killed it
(`docs/evidence-R13/pkill-comm-live.log`). Three of those recipes clean up
targets that run a shipped binary (`cy`, `jm`, `xe`), so a leftover server
keeps the fixed port and the next target queries a server that never read its
config: a false failure, or worse a false pass. All 17 occurrences now match
by prefix; the same prefix rule fixed the `preflight` guard one addendum ago.

**Addendum 2, same round (R13/D67) -- the wing is not parallel-safe.** Running
`make -k all` while the focused nine-target loop was also running produced a
`[FAIL]` that did not exist (target `cy`, on a tree whose `cy` passes 8/8 when
run alone); the raw excerpt is kept in
`docs/evidence-R13/concurrency-collision.log`. Every target here binds fixed
ports (cy 16400-16410, ce 15700-15703, oh 16996/16997, pq 15900/15901 ...) and
writes fixed work files (`.cyc.cfg`, `.cyc-srv.log`, ...), so two runs
corrupt each other instead of merely being slow -- that teaches an engineer
to treat FAILs as noise, which is worse than not having the rig. A second
run of the same wing was later disturbed by a stray `pkill` during the
preflight test, hitting `cachecw`; it too was re-run alone and passes.
`preflight` is now the first prerequisite of `all` and refuses to start when
a MultiCS server is already alive; a per-target lock is still worth having
(M33). The first version of that check matched `comm` exactly and was blind
to the shipped binaries -- `comm` is truncated to 15 characters, so
`bin/multics-r82a-stats-x64` appears as `multics-r82a-st` and slips past a
`grep -cx multics` (measured live, PREFLIGHT_RC=0 with the server running).
The check now matches the prefix `^multics` and was re-verified on a real
server process in the middle of the wing run (exit 2, refusal), see
`docs/evidence-R13/preflight-live.log`. Any process-name matching in this
project has to be prefix-based for the same reason. The frozen side-by-side is: wing 51 targets, loop targets
69 checks, ce 10, oh 3, unit 1352.


**Addendum, same round (R13/D67) -- the rest of what the sweep found and
fixed.** (1) The one-byte stack overrun in the HTTP header reader:
`parse_http_request()` read up to `sizeof(buffer)` into `buffer[2048]` and
then wrote the terminator at `buffer[size]`, so a first packet of exactly
2048 bytes put one byte of stack outside the array -- remotely reachable
before any authentication, and the POST body loop below it had always
guarded itself, which is how the omission was spotted. Both `recv()`
calls now reserve the terminator byte (`httpserver.c:430` and `:446`), and
a new live target `oh` (stability.mk:4902-4933, wired into `all`) floods a
3000-byte header and asserts the three outcomes that matter: the server
survives, its log carries no crash marker, the next request answers 200
(3/3, `docs/evidence-R13/oh-post-R13.out`). (2) Two `=` that should have
been `==` in the CCCAM/FREECCCAM client-info path (`srv-cccam.c:534`,
`srv-freecccam.c:251`): they WROTE 'H' and 'O' into the client's own
version string and left `sendversion` decided by byte 28 alone; the intent
of the original test is undocumented, so the minimal repair is the
comparison, flagged as M30. (3) `config.c:3015-3020` and `:3301-3306`:
the two CCCAM-client info blocks `strcpy`'d a config line of up to 255
bytes into `info->name[32]` (heap-struct overrun from config content) and
scanned `str[strlen(str)-1]` when the value could be empty (a read at
`str[-1]`); both blocks now use bounded copies and an `i>=0` guard.
(4) `cachequeue.h`: the comment claimed an out-of-range length counted as
a drop; the code (rightly) counts only ring-full drops, so the comment was
corrected and the unit test pins the behaviour. (5) The six exchange sites
carried a stale "OBSERVE-ONLY ... changes no score and disables nothing"
paragraph above a proof path that scores and purges; rewritten to say what
R13 changed (who may testify) and what the code does. (6) `cwlog.h`: the
two static name helpers are now `__attribute__((unused))` so the unit
build is warning-clean. (7) `tests/stability.mk`: the `tl` target's two
wait loops grepped a file the background peer had not created yet (a raw
grep error in the wing log, seen in the first full run) -- `2>/dev/null`.
(8) `.gitignore` added: build products (`x64/`, `dist/`, `*.o`), rig
scratch (`tests/.*`, logs, pids) and `multics.log`; `bin/` is deliberately
NOT ignored because the shipped binaries are tracked and pinned.
(9) The manifest was re-frozen over the whole set including the two
recovered test sources, the two `queue` flavours that had never been
pinned since R6, this round's documents and `docs/evidence-R13/`;
`md5sum -c MANIFEST-md5` exits 0. Final fingerprints: stock
**superseded -- see addendum 4**: the sources changed again later in the
same round (the telnet guard, the two checked allocations, the build-flag
cleanup), so every flavour was rebuilt and re-hashed one last time and the
final family is stock `5be15281...`, stats `0c0a5841...`, queue `a8e9b161...`,
queue-stats `e325203d...`, dev `a0be49c6...` (REPORT-R13-ar.md §8.3). The
`88a3bf29` family in STATUS and the `9bb6a247` family here are both
intermediate builds of R13; only §8.3 is authoritative.

## D68 -- TASK R14a, the console that could be held open for free: idle
## timeout, session ceiling, and the refused `ce` dependency (2026-10-08)

**Situation.** `telnet.c` had no `SO_RCVTIMEO` and no session limit, and the
allow-list default is empty (= everybody). One unauthenticated client could
connect, send zero bytes, and hold a thread for ever; N clients held N
threads. This was the first item of the R14 plan (REPORT-R13-ar.md §6, M35)
because it was the only one-sided, unauthenticated resource exhaustion left
in the tree.

**Decision.** `TELNET TIMEOUT` (default 300 s, 0 = off) is armed on the
socket before the first read and every console read goes through
`telnet_recv()`, which says `[TELNET IDLE]` out loud when the timer fires
instead of letting a silent disconnect look like the client left.
`TELNET MAXCLIENTS` (default 64, 0 = off) is a gate at the accept door, before
the thread is created, counted by a counter with its own mutex that is never
held across I/O; the refusal is one line ("too many sessions, bye.\r\n") plus
`[TELNET LIMIT]` with the number. Both values are read from the config and
copied on the SIGHUP path as well.

**Also fixed, same round.** `tests/stability.mk`: the `ce` recipe drives
`$(NCCLIENT)` twice in the logs but never declared it as a prerequisite, so
`make ce` failed with "not found" buried in `.ce-nc2.out` (F21/O30).

**Verification.** New live target `tl2` 7/7 (a silent session is closed by the
server itself in 3 s and logs why; three open sessions and the fourth is
refused with one sentence, no thread created; seats come back afterwards).
Instrument: `tests/telnetprobe.py` (idle/cap/login modes -- the recipe owns
the verdict, the tool prints one line). Evidence: `docs/evidence-R14/tl2-*`,
`ce-after-ncclient-dep.out`; `FULLWING_R14_RC=0` on the full wing of that
round.

## D69 -- TASK R14b, the packed-member family: from 12 warnings to zero, and
## the undefined behaviour UBSan proves was there (2026-10-08)

**Situation.** `-fpack-struct` is kept on purpose (it is the wire contract),
which gives `AES_KEY`, `SHA_CTX` and `struct MD5Context` alignment 1. Twelve
`-Waddress-of-packed-member` warnings were the visible part: `sha1.c:211,213`,
`md5.c:224,227`, `aes.c:691,792,842,1033`, `config.c:1707,1812,1837,1874`.
The invisible part is that the code took a member address and used it through
a 4-byte-aligned pointer type: `rk = key->rd_key` in AES, the state parameter
of both digest transforms, and `&cli->ucrc` in the config parser. On x86-64
that only costs a warning; on arm/mipsel/sh4 (all three are shipped builds in
`bin/`) it is SIGBUS.

**Decision.** `memcpy` an aligned local, at the point where the member address
would otherwise meet the aligned type. `SHA1_Transform`/`__md5_Transform` take
`void *statep` and copy the state in and out; AES gets two internal workers
taking an aligned schedule buffer plus two wrappers that copy the result into
the member (zeroing the buffer first so the unused tail of the schedule is
deterministic instead of stack garbage), and `AES_encrypt`/`AES_decrypt` copy
the schedule to an aligned local before touching it; the camd35 data path gets
`camd35_init_data_store()` in `msg-camd35.c`, whose parameters are `void *` on
purpose so that no 4-byte-aligned pointer is ever derived from a packed
member.

The alternative -- `__attribute__((aligned(4)))` on `rd_key` in `aes.h` -- was
measured and rejected: it keeps the size (244) but changes the alignment of
every struct that embeds an `AES_KEY` and produced **41 new warnings** across
`cli-camd35.c`, `cli-cs378x.c`, `srv-camd35.c`, `srv-cs378x.c` and
`cacheex.c`, i.e. it moves the problem instead of solving it. (`#pragma
pack(push,4)` is not an option either: GCC ignores it under `-fpack-struct`
and warns `-Wpragmas`.)

**Verification.** Clean rebuild `rm -rf make-x64/x64 && make -C make-x64 link`
=> 0 warnings / 0 errors (was 12). `make test` => 1382 ok / 0 failed / 28
targets (was 1352/27): the new `make-x64/test_m1align.c` adds 30 checks --
nothing in the suite touched MD5/SHA-1/AES before it, even though those three
produce `ecmd5` and the camd35 handshake keys. It builds with `-fpack-struct`
proves the offsets really are odd (`%4 == 1`), then runs FIPS-197 C.1
(AES-128, both directions), RFC-1321 (MD5) and RFC-3174 (SHA-1) through
under-aligned holders, compares the store path against the direct path byte by
byte, and checks the schedule tail is zeroed. `docs/evidence-R14/`
`m1-ubsan-probe.sh`: a single driver through an object at an odd offset
reports **47 misaligned accesses and exit 1** on the pre-M1 sources
(`aes.c:700-715,848-851,934-1015`, `sha1.c:144-148,173-177`) and **exit 0 with
zero reports** on the current tree, with identical functional output. This is
the step that turns O1 from warning hygiene into removed undefined behaviour.
Dev fingerprint `cad188f4c9d82e7e0da365fe0a09a776`.

## D70 -- TASK R14b (found by the new test), the SHA-1 transform was writing
## into its caller's buffer -- a SIGSEGV waiting for the first long input
## (2026-10-08)

**Situation.** The FreeBSD SHA-1 expands the message schedule in place through
`block`, and the arm that computed it was `block = (CHAR64LONG16*)buffer`:
`SHA1_Transform` wrote 64 bytes into whatever buffer it was handed. Its only
in-loop caller is `SHA1_Update`, which passes `const uint8_t *data` straight
through (`sha1.c:223`). Any input of 64 bytes or more living in read-only
memory (a literal, a `const` table) therefore dies with SIGSEGV, and any
writable input is silently corrupted -- the caller's ECM/ECM-like buffer would
carry the expanded schedule afterwards. Nothing had ever caught it because
every `SHA1_Update` call in the tree is 16 bytes (`cli-cccam.c:96`,
`srv-cccam.c:285`, `srv-freecccam.c:98`), which never enters the block loop,
and no test in the suite passed more than 64 bytes to MD5 or SHA-1.

**Decision.** The block travels through a local, aligned copy
(`uint8_t workspace[64] __attribute__((aligned(4)))`) inside
`SHA1_Transform`. Not the file's old `SHA1HANDSOFF` arm: that used a *static*
workspace, which would have made the transform thread-unsafe -- and the rest
of this project is threaded. `block->l[i]` is now a 32-bit access on a
guaranteed-aligned address (it was also reaching into a packed context member
before). Matrix: MD5 does not have this defect -- it builds `x[16]` locally and
reads the block byte-wise -- and the same probe proves it.

**Verification.** `docs/evidence-R14/d70-sha1-rodata.sh` (re-runnable):
against `src/sha1.c` at `27bb871` the probe exits **139** (SIGSEGV); against
the current tree it prints the digest, `input-mutated=0`, and the 128-byte
(two-block) digest equals `hashlib.sha1` byte for byte:
`272049b5add909ae0ed72e347781e50e3670f4dd`. Pinned as a test case in
`make-x64/test_m1align.c` (read-only 128-byte input for both MD5 and SHA-1,
plus "the caller's block is byte-identical afterwards").

## D71 -- TASK R14c, the `jm` target that failed only inside the wing: a
## recipe race, not a server bug (2026-10-08)

**Situation.** The first full wing on the M1 tree (R14b-1) ended with
`make: *** [stability.mk:4496: jm] Error 1`. Phases 1-5 passed; phase 6 could
not find the "DISTRUST" escalation it exists to read; phases 7-8 read the
ladder record out of `/json` and failed because there was nothing to read.
The same target passed when run alone -- both before and after the wing -- so
the difference had to be timing, not code. The recipe drove its flood loop (20
tries) in the first seconds after the cachepeer B process started, while the
server only asks B for DCWs after B's ping round-trip:
`cache: Peer (127.0.0.1:<port>) come Online`. In a wing the preceding targets
leave the machine busy, so B was still offline for all 20 tries; a flood
nobody answers never produces a `TYPE_REQUEST`, and no escalation can follow.

**Decision.** Add a bounded readiness wait before the flood loop (20 s max,
the same `grep "come Online"` the operator would use) and raise the try-loop
sleep from 1 s to 3 s -- the value the `pr` target already uses. No assertion
changed, no server source touched. The target still demands its eight phases
and the escalation it reads is still live evidence from the running document.

**Verification (before M38 added the opt-in).** `make -C tests jm` alone:
rc 0, 8/8, both before and after the edit. The equivalent current invocation
is `make -C tests MCS_WING_OK=1 jm`; the post-M38 full wing below also runs
`jm` under this explicit opt-in. The earlier wing evidence is
`docs/evidence-R14/wing-targets-post-m1.log`; the two measured arms sit side
by side in `docs/evidence-R14/d71-jm-in-suite.log` (before: phases 6-8 FAIL +
`Error 1` in the R14b-1 wing; after: 8/8).

**Not a server bug.** Said out loud because the red target looked like one:
the failing run's own log shows B never reached "come Online" before the flood
loop ended, so the server had nothing to report to `/json`. The fix lives
entirely in `tests/stability.mk`.

## D72 -- TASK R14d, build hygiene: the last two C++/C-only flags, the two
## intentional truncation warnings, and a document row that pointed at a log
## that was never kept (2026-10-08)

**Situation.** Three small things that the round's measurements made visible.
(1) `make-cross/Makefile` still carried `-fpermissive` and
`-Wno-return-mismatch`, the flags F18 removed from `make-x64` (O25). (2) The
unit build prints two `-Wformat-truncation` warnings
(`src/cacheguard.h:136` reached from `make-x64/test_cacheguard.c`, and
`src/statsline.h:191` reached from `test_phase1cfg.c`) -- both because those
tests hand the formatter a deliberately small buffer to exercise the
truncation edge; the library build itself is 0/0. (3) `REPORT-R13-ar.md` §8.1
listed `docs/evidence-R13/wing-targets-post-r13.log`, which does not exist:
the concentrated nine-target loop left only its nine-line summary
(`wing3.log`) in the tree.

**Decision.** Remove both flags from `make-cross` (the flag set is now the
same as `make-x64`'s minus `-m64`). Keep the two truncation warnings visible
rather than hiding them with `-Wno-format-truncation`: they mark exactly the
buffer-size questions the tests ask, and a blanket suppression would also hide
the same pattern if it ever appears in `src/`. Repair the §8.1 row to name
`wing3.log`, state openly that the detailed log was not kept, and give the
command that regenerates it (`make -C tests MCS_WING_OK=1 cy au pr jm pq sk cn vl ri`; such
a log was produced on the M1 tree as
`docs/evidence-R14/wing-targets-post-m1.log`).

**Also in the same pass (M36).** The ladder's stage names were spelled out
three times -- a dead `stage_name()` in `peerrep.c`, `mj_stagename[]` in
`monjson.c`, `sn[]` in `telnet.c` -- so renaming a stage would have left two
copies behind. They now come from one `peerrep_stage_name()` in `peerrep.h`
(`__attribute__((unused))` so includers that do not use it stay
warning-clean). The old tables returned the same four words for the same
values, and the `/json` document and the `PEERREP` command were compared
before/after on the same inputs: byte-identical.

**Verification.** The new flag set compiles three representative sources
natively (`sha1.c`, `aes.c`, `config.c`) with zero warnings; a real cross build
still needs the cross toolchain and stays with M2 (O2). The truncation
warnings are counted in `docs/evidence-R14/m1-unit-suite.log` (2), the library
build in `m1-freshbuild.log` (0).


## D73 -- TASK R14e / M38, make the destructive test wing opt-in (2026-10-09)

**Situation.** `tests/stability.mk` uses process-wide `pkill -9 '^multics'`
for cleanup. The old `preflight` protected only `make all`; invoking one
kill-bearing target directly bypassed it. On a host running a production
`multics` daemon, a test could therefore terminate the unrelated daemon.

**Decision.** `tests/Makefile` now documents that the live wing is for a
dedicated, disposable development/test machine, never production. The explicit
opt-in is `MCS_WING_OK=1`; without it `preflight` fails with status 2. The
parse-time guard rejects `all` and all 31 kill-bearing goals before any recipe,
even under `make -k`; those targets also depend on `preflight`, which checks for
an already-running `multics*` process. All 70 process-wide kill sites in
`stability.mk` go through `MCS_KILL_MULTICS`, whose default expansion also
refuses to kill (defense in depth if a future target is omitted from the list).
Current safe invocation examples are
`make -C tests MCS_WING_OK=1 all` and
`make -C tests MCS_WING_OK=1 jm`.

**Verification.** `docs/evidence-R14/m38-guard-checks.log`: default
`preflight`, `all -k`, and all 31 direct kill-bearing targets refused with rc 2;
the opt-in preflight passed with no active `multics` process. Static audit: 70
macro sites, 31 matching guarded targets, zero ungated literal sites. Standalone
`jm` with the flag is 8/8 (`m38-jm-optin.log`). The full wing was then run with the
opt-in (`docs/evidence-R14/m38-explicit-optin-wing.log`): rc 0, 453 result
lines beginning with `[ ok ]`, zero result lines beginning with `[FAIL]`.
The 20 `[FAIL]` tokens elsewhere in that raw log are text echoed from recipe
bodies, not failed assertions.
