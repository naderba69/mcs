# Contributing to MultiCS r82a

Thank you for your interest in contributing to MultiCS r82a! This document provides guidelines and information for contributors.

---

## Project Status

**Status:** ✅ Complete — Phase R13–R20 closed (D76, 2026-10-14).

The project has completed 21 hardening missions (R1–R21) and is in maintenance mode. New features are not planned unless explicitly requested by the project owner.

**What we accept:**
- Bug fixes (with live proof)
- Documentation improvements
- Test coverage additions
- Build system fixes

**What we don't accept (without explicit approval):**
- New features
- Architectural changes
- Thread model changes
- Cache layout changes

---

## Ground Rules (GR1–GR10)

All code must respect the project's ground rules:

1. **GR1**: Source attribution before scoring — `dcwsrctype/dcwsrcid` set at delivery only
2. **GR2**: Timeout ≠ failure — separate counters for timeout vs refusal
3. **GR3**: No punishment on soft evidence — only hard proofs (`LEDGER_PROOF`, reuse proof) act immediately
4. **GR4**: Detail in every log line — (type, ID, CAID, PROVID, SID)
5. **GR5**: MIN-CARD-LATENCY for newcamd replies only — never in cache path
6. **GR6**: Key on `ecm_hash`/`ecmd5`, not SID — CW identity is byte signature
7. **GR7**: Probes never reach clients — all test traffic in `tests/`, not `src/`
8. **GR8**: Every harsh action has a reversible log line — MARK/TRUST/GUARD with windows
9. **GR9**: Hot path has no blocking and no unbounded growth — fixed-size LRU tables
10. **GR10**: CW from a peer is never re-pushed — `clustredcache.c:2358-2371`

---

## Development Workflow

### 1. Setup

```bash
git clone https://github.com/naderba69/mcs.git
cd mcs
cd make-x64
make test  # verify build
```

### 2. Make Changes

- **One mission per round** — small, focused changes
- **Live proof required** — demonstrate the bug before fix, verify after
- **No silent rewrites** — incremental diffs only
- **English for logs/messages** — Arabic comments are OK

### 3. Test

```bash
# Unit tests (required)
make -C make-x64 test

# Integration tests (required)
make -C tests all

# Soak test (for performance changes)
cd tests && SOAK_SECONDS=90 bash soak.sh

# Memcheck (for memory-related changes)
cd make-x64 && bash memcheck.sh units
```

### 4. Document

- **Arabic report** (`docs/REPORT-xxx-ar.md`) — what/why/verification/rollback
- **Decision log** (`docs/DECISIONS.md`) — append new D-entry
- **Status update** (`docs/STATUS.md`) — add update block
- **Roadmap update** (`docs/ROADMAP-R13-ar.md`) — mark task ✅

### 5. Commit

```bash
git add -A
git commit -m "Rxx (Dyy): brief description in Arabic

- What changed
- Why (problem statement)
- Verification (test results)
- Fingerprints (if binaries changed)
- MANIFEST updated"
```

### 6. Submit

```bash
git push origin your-branch
# Open pull request on GitHub
```

**Note:** The project owner has requested **no pull requests** for internal work (`«واصل الانتاج ولا تقم ب pr»`). External contributors should still use PRs.

---

## Coding Style

### C Code

- **Standard:** gnu89 (not C99, not C11)
- **Indentation:** tabs (not spaces)
- **Line length:** 100 chars max
- **Functions:** short, focused, single return point preferred
- **Headers:** header-only layers use `static inline` functions

### Naming

- **Functions:** `snake_case` (e.g., `cache_setdcw`, `ledger_note_delivery`)
- **Macros:** `UPPER_CASE` (e.g., `DCW_SOURCE_CACHE`, `LEDGER_PROOF`)
- **Structs:** `struct name_data` (e.g., `struct cache_data`, `struct ecm_data`)
- **Files:** `lowercase.c` / `lowercase.h`

### Comments

- **Arabic OK** for explanations
- **English required** for log messages and user-facing strings
- **Doxygen-style** for public APIs (optional)

### Error Handling

- **Return codes:** 0 = success, negative = error
- **Log before return:** every error path logs with `debugf` or `cs_log`
- **No silent failures:** if it can fail, it must say so

---

## Testing Requirements

### Unit Tests

Every new pure function needs a unit test:

```c
// make-x64/test_xxx.c
#include <stdio.h>
#include "../src/xxx.h"

int main() {
    int pass = 0, fail = 0;
    
    // Test 1
    if (xxx_function(input) == expected) {
        pass++;
        printf("  [ ok ] test description\n");
    } else {
        fail++;
        printf("  [FAIL] test description\n");
    }
    
    printf("== %d/%d passed, %d failed ==\n", pass, pass+fail, fail);
    return fail ? 1 : 0;
}
```

Add to `make-x64/Makefile`:

```makefile
test: ... x64/test_xxx ...
    ...
    ./x64/test_xxx

x64/test_xxx: test_xxx.c ../src/xxx.h
    $(CC) -O2 -m64 -Wall -Wextra -std=gnu89 -o $@ test_xxx.c
```

### Integration Tests

Every new server behavior needs a live test:

```makefile
# tests/stability.mk
.PHONY: xxx
xxx: $(BIN) $(HARNESS)
    @echo "== scenario xxx =="
    # Setup config
    # Start server
    # Run client/peer
    # Verify counters/logs
    @echo "[ ok ] xxx"
```

### Acceptance Criteria

- **`make test` green** — 1643+ checks, 0 failures
- **`make -C tests all` green** — 530+ scenarios, 0 FAIL
- **Fingerprints updated** — if `src/` changed, rebuild all flavours
- **MANIFEST updated** — `md5sum -c MANIFEST-md5` must pass

---

## Documentation Requirements

### Arabic Report (`docs/REPORT-xxx-ar.md`)

Every mission needs an Arabic report:

```markdown
# تقرير Rxx — العنوان (التاريخ)

**المهمة:** وصف المهمة  
**القرار:** Dyy  
**الحالة:** ✅ مُنجز

## 1. المشكلة
وصف المشكلة التي تُحل

## 2. القرار (Dyy)
ما الذي تغيّر ولماذا

## 3. التحقق
| المقياس | القيمة | الحالة |
|---|---|---|
| الوحدات | X/Y | ✅ |
| التكامل | X/Y | ✅ |
| البصمات | md5 | ✅ |

## 4. التراجع
كيف تُرجع التغيير إذا لزم
```

### Decision Log (`docs/DECISIONS.md`)

Append a new D-entry:

```markdown
## Dyy — TASK Rxx, the thing (date)

**Situation.** What problem exists.

**Decision.** What changed and why.

**Verification.** Test results, fingerprints.

**Rollback.** How to undo.
```

---

## Build System

### Flavours

The project builds five flavours:

1. **stock** — `make release` — baseline r82a behavior
2. **stats** — `make release-stats` — with `/cwlog` page
3. **queue** — `make release-queue` — with queue support
4. **queue-stats** — `make release-queue-stats` — queue + stats
5. **dev** — `make` — development build

### Fingerprints

Every `src/` change updates fingerprints:

```bash
cd make-x64
make release release-queue release-stats release-queue-stats
cp x64/multics ../bin/multics-r82a-x64
md5sum ../bin/multics-r82a-*-x64
```

Update `MANIFEST-md5`:

```bash
git add -A
python3 -c "
import hashlib, subprocess, os
files = subprocess.check_output(['git', 'ls-files'], text=True).strip().split('\n')
# ... (see MANIFEST-md5 generation script)
"
md5sum -c MANIFEST-md5
```

---

## Common Pitfalls

### 1. Flavour Trap

`/cwlog` route is `#ifdef MCS_DCSTATS` — stock binary closes connection silently. Use `make release-stats`, not plain `make`.

### 2. CW Reuse Proof

A CW repeated across sids is convicted **before** send loops. Every scenario push needs a unique checksum-valid CW.

### 3. Chained-`if` in `#ifdef`

Keep both flag states compiling with `else if (0)` shim.

### 4. Parallel Test Runs

Never run two `make -C tests <target>` concurrently — shared harness binaries/ports collide. Sequential only.

### 5. MANIFEST Regeneration

`git add` new files **before** regen (`git ls-files` drives entries). Regen strictly last before commit.

---

## Reporting Issues

### Bug Reports

Open a GitHub issue with:

- **Symptoms** — what you see (logs, screenshots)
- **Config** — `multics.cfg` (redact credentials)
- **Binary** — which flavour, MD5 fingerprint
- **Steps** — how to reproduce
- **Expected** — what should happen

### Security Issues

**Do not open a public issue.** Email the maintainer directly.

---

## Code of Conduct

- **Be respectful** — technical disagreements are fine, personal attacks are not
- **Be patient** — responses may take days
- **Be thorough** — incomplete reports will be closed

---

## Questions?

- **Documentation:** [docs/INDEX-ar.md](docs/INDEX-ar.md)
- **Status:** [docs/STATUS.md](docs/STATUS.md)
- **Architecture:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

---

## License

By contributing, you agree that your contributions will be licensed under the same license as the project (open-source).
