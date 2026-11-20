#!/usr/bin/env python3
"""
Band 119 source audit: file-scope MUTABLE state living in a header.

`static` at file scope means internal linkage, so every translation unit that
includes the header gets its OWN copy of the object. For an observer header that
is written in one .c and read in another, the reader therefore reads a copy that
no code path ever writes -- and on /user-trace that prints as a clean, perfectly
plausible ZERO.

This is not hypothetical. The 2026-11-19 ~21:27 snapshot (uptime 8h28m) printed
"Wrote=0 Read=0" for the band-115 channel memory with the feature ON. A zero of
that shape cannot distinguish "the path never ran" from "it ran, into a copy this
file cannot see" -- and the band was designed on the first reading. The fix was
one program-wide instance (MCS_CHAN_PREVCW_IMPL in src/cache-chan-prevcw.h).

So every header with file-scope mutable state that reaches 2+ translation units
must either (a) own its state through an explicit *_IMPL macro with extern for
everyone else, or (b) sit in the allowlist below with a written reason. Silent
duplication is what this guard forbids.

Run: python3 tests/staticstate.py
Exit: 0 clean, 1 violation, 2 bad usage.
"""

import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"

# Debt, named (band 119). Each entry: header -> why it is not fixed in this band.
# The rule is deliberate: I only convert a header when a field decision depends
# on the cross-TU pair. Fixing the rest is real work and needs its own band, with
# its own evidence, not a sweep dressed up as hygiene.
ALLOWLIST = {
    "cwpair.h": "band 82 twin table: written in clustredcache.c, consulted for the "
                "offer in th-ecm.c, snapshotted for the page in main.c through "
                "field-status.h. Fixing it changes an armed gate, so it needs a "
                "live-pair test first -- BAND 120 candidate.",
    "field-status.h": "declares extern prototypes only for the page snapshot; the "
                      "duplicate definition question is part of the cwpair item above.",
    "supersmartfilter.h": "config-only statics: written and read in config.c; the "
                          "other includers call pure predicates.",
    "advanced-optimizations.h": "self-contained predicate header; its statics are "
                                "caches that are correct per-TU (lossy, not wrong).",
    "blackscreen-detector.h": "R48 observe-only surface, reported through its own "
                              "getter in main.c only.",
    "cache-intelligence.h": "per-TU state; CI VALIDATE is OFF in the field cfg, so "
                             "no decision currently rests on the split.",
    "cw-postvalidate.h": "R48 observe-only; counters printed from the same TU that "
                         "writes them.",
    "dcwstrict.h": "strict-mode bookkeeping lives in main.c on both sides today.",
    "implicit-bs-detect.h": "R48 observe-only; single writer/reader TU in main.c.",
    "r48-source-reputation.h": "one flag plus counters, read for the page from "
                               "httpserver.o; named, not yet fixed.",
}

STATIC_DECL = re.compile(
    r"^static\s+(?!const\b)(?!.*\binline\b)"
    r"(?:unsigned\s+|signed\s+|const\s+)*"
    r"(?:struct\s+\w+\s*\*?\s*|union\s+\w+\s*\*?\s*|[A-Za-z_]\w*\s*\*?\s*)"
    r"(?P<name>\w+)\s*(?:\[[^\]]*\])?\s*(?P<init>=|;)",
    re.M,
)
TYPE_ONLY = re.compile(r"^static\s+(?:struct|union)\s+\w+\s*\{")
# Owner-macro style (band 119): `FOO_EXT struct bar g_baz;` -- `static` is gone on
# purpose, so it must still be recognised as cross-TU state, or the audit would
# credit the fix by omission instead of by shape.
EXT_DECL = re.compile(r"^\w*EXT\s+(?!typedef)(?:struct\s+\w+|\w+)\s*\*?\s*(?P<name>\w+)\s*(?:\[[^\]]*\])?\s*(?:=|;)", re.M)


def ext_state(text):
    """Objects declared through an owner-macro (X_EXT) instead of `static`."""
    return [m.group("name") for m in EXT_DECL.finditer(text)]


def mutable_statics(text):
    """File-scope static objects (not functions, not const, not type definitions)."""
    out = []
    for m in STATIC_DECL.finditer(text):
        head = m.group(0)
        if TYPE_ONLY.match(head):          # `static struct foo { ... };` = a typedef-ish
            continue
        # a function definition would have '(' before '=' or ';' -- rejected by the regex
        out.append(m.group("name"))
    return out


def main():
    tus = {p.name: p.read_text(errors="replace") for p in sorted(SRC.glob("*.c"))}
    if not tus:
        print(f"FAIL: no translation units found under {SRC}")
        return 2

    problems = []
    checked = 0
    n_ok = n_debt = 0
    for hdr in sorted(SRC.glob("*.h")):
        text = hdr.read_text(errors="replace")
        names = mutable_statics(text)
        shared = ext_state(text)
        if not names and not shared:
            continue
        inc = [n for n, t in tus.items()
               if re.search(r'#\s*include\s*["<]' + re.escape(hdr.name) + r'[">]', t)]
        # a header may also reach a TU transitively; count direct includes only and
        # say so, so the audit stays explainable.
        if len(inc) < 2:
            continue
        checked += 1
        has_impl = re.search(r"#ifdef\s+\w*IMPL\b", text) and re.search(
            r"\bextern\b", text)
        if has_impl and shared and not names:
            n_ok += 1
            print(f"OK   {hdr.name}: {len(inc)} TUs ({', '.join(inc)}) -- single instance "
                  f"via *_IMPL owner macro: {', '.join(sorted(set(shared))[:4])} "
                  f"(no `static` objects left to duplicate)")
            continue
        if has_impl:
            n_ok += 1
            print(f"OK   {hdr.name}: {len(inc)} TUs ({', '.join(inc)}) -- single instance "
                  f"via *_IMPL owner macro: {', '.join(sorted(set(names))[:4])}")
            continue
        if hdr.name in ALLOWLIST:
            n_debt += 1
            print(f"DEBT {hdr.name}: {len(inc)} TUs -- duplicated statics "
                  f"{', '.join(sorted(set(names))[:4])} :: {ALLOWLIST[hdr.name]}")
            continue
        problems.append((hdr.name, inc, sorted(set(names))))

    if problems:
        print()
        for name, inc, names in problems:
            print(f"FAIL {name}: mutable file-scope statics duplicated across "
                  f"{len(inc)} TUs ({', '.join(inc)}): {', '.join(names)}")
        print("\nFix: make one TU the owner with an #ifdef <NAME>_IMPL / extern split "
              "(see src/cache-chan-prevcw.h), or add the header to ALLOWLIST with a "
              "written reason naming the band that owes the work.")
        return 1

    print(f"\nstaticstate audit: {checked} multi-TU stateful headers checked "
          f"({n_ok} single-instance, {n_debt} named debts, {len(problems)} violations).\n"
          "Note: a header whose objects are NOT static (a true multiple definition) "
          "fails at link time -- this guard only hunts the silent kind.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
