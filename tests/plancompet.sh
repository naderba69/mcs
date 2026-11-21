#!/bin/sh
# Band 121 addendum: docs/PLAN-COMPETITION-ar.md is a multi-phase plan whose
# motivation rests on three facts about TODAY's tree. Prose rots silently when the
# code moves and nobody re-reads the plan -- so the plan's load-bearing claims are
# asserted here. A FAIL is not a bug: it means the plan text must be updated to
# match the code, and only then may this guard be relaxed.
set -eu
cd "$(dirname "$0")/.."
python3 - <<'PY'
import re, sys
from pathlib import Path

txt = Path("docs/PLAN-COMPETITION-ar.md").read_text(encoding="utf-8")
src_c = {f.name: f.read_text(encoding="utf-8", errors="replace") for f in Path("src").glob("*.c")}
fails = []

def first_caller(fn):
    pat = re.compile(r"\b" + fn + r"\s*\(")
    for name, body in src_c.items():
        if pat.search(body):
            return name
    return None

# (1) the cache-intelligence ranking API is orphaned: the plan calls it a
#     "متحف ظاهري" and defers the wire-or-retire decision to phase ب2.
caller = first_caller("ci_get_best_peers")
if caller and "متحف" in txt:
    fails.append(f"ci_get_best_peers now has a caller ({caller}) but the plan still says "
                 "nobody consults it -- update section 0-B and phase ب2")

# (2) in_cycle is a literal 1 at the single cwpostval_validate call site: that is
#     the plan's defect 0-د(1) ("نقطتان مجانيتان") and phase ب1.
sd = src_c.get("setdcw.c", "")
i = sd.find("cwpostval_validate(")
window = sd[i:i + 700] if i >= 0 else ""
hard = bool(re.search(r"1\s*,?\s*/\*\s*in_cycle", window))
if not hard and "نقطتان مجانيتان" in txt:
    fails.append("setdcw.c no longer passes a literal for in_cycle, but the plan still "
                 "lists the free-two-points defect -- update 0-د and phase ب1")
if hard and "نقطتان مجانيتان" not in txt:
    fails.append("the plan dropped the free-two-points defect while setdcw.c still passes "
                 "a literal for in_cycle")

# (3) two headers are orphans (zero callers in src/*.c): the plan lists deleting or
#     annotating them as phase ب5.
for hdr in ("cwchannel", "cwconfidence"):
    called = [n for n, body in src_c.items()
              if re.search(r"\b" + hdr + r"_\w+\s*\(", body)]
    if called and "يتيمة" in txt:
        fails.append(f"src/{hdr}.h now has callers ({', '.join(called)}) but the plan still "
                     f"calls it an orphan -- update 0-ب and phase ب5")

if fails:
    for f in fails:
        print("FAIL:", f)
    print("note: this guard protects the PLAN FROM BEING WRONG, not the code from being changed")
    sys.exit(1)
print("plancompet: PASS (the plan's three load-bearing facts still match the tree)")
PY
