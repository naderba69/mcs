#!/usr/bin/env python3
"""R56: build a CANDIDATE profiles file from the production one, without
changing any effective behaviour except the timing rule below.

Input : multics-r24-optimized.cfg   (the production file, read only)
Output: multics-r56-fresh.cfg       (candidate; the production file is NOT touched)

Rules (each one is listed in docs/FRESH-CONFIG-R56-ar.md):

 1. Every key that a profile inherits from a `DEFAULT ...` line in the global
    section is written out explicitly inside the profile, with the SAME value
    the DEFAULT line gives. Effective behaviour does not change; the profile
    now says everything about itself.
    Keys with no DEFAULT line (DCW MINTIME, ECM CHECK LENGTH, ...) are left
    alone: their default is not stated in the file, and this tool does not
    guess it.

 2. Server window (black-screen fix, measured):
      - a profile at SERVER TIMEOUT 800 moves to 1000 (= the file's own
        DEFAULT SERVER TIMEOUT). The 800 class came from a small sample.
      - CAID 1814 (MEO PT, measured R56 from a decode log): its valid CWs
        arrived at 917, 948 and 1040 ms. They all came after 800 ms, so every
        valid CW was refused. Window 1200 ms covers all three.
    Profiles already above 800 (the R45 slow class) are NOT touched.

 3. Nothing else changes: PORTs, CAIDs, PROVIDERS, every feature switch,
    every CACHE / CACHEEX value, and all comments stay as they are.

Run:  python3 tests/fresh_cfg.py [input] [output]
"""
import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "multics-r24-optimized.cfg"
DST = sys.argv[2] if len(sys.argv) > 2 else "multics-r56-fresh.cfg"

# Keys that a profile may inherit from a DEFAULT line. Only these are made explicit.
INHERITABLE = [
    "DCW CHECK", "DCW MAXFAILED", "DCW RETRY", "DCW CYCLE_CHECK",
    "ENABLE SKIPCWC", "ENABLE CACHE", "ENABLE CACHEEX",
    "CACHE STATIC", "CACHE TIMEOUT", "CACHE SENDREQ", "CACHE SENDREP", "CACHE RESENDREQ",
    "SERVER FIRST", "SERVER TIMEOUT", "SERVER INTERVAL", "SERVER VALIDECMTIME", "SERVER MAX",
    "CACHEEX VALIDECMTIME", "ACCEPT NULL PROVIDER",
]
# NOT inheritable on purpose: CACHEEX MAXHOP is clamped to 2 when written inside a
# profile (config.c; cfgcheck: "use CACHEEX MAXHOP_LG"), so writing the DEFAULT value
# would change behaviour. MAXHOP_LG is left as the operator wrote it.

# Measured window overrides (CAID -> server timeout ms). Source: docs/FRESH-CONFIG-R56-ar.md.
MEASURED_TIMEOUT = {"1814": 1200}
STOCK_TIMEOUT_FROM = 800   # the R45 "fast" class value that moves to the file's default
DEFAULT_TIMEOUT = 1000     # = DEFAULT SERVER TIMEOUT in the global section

KEYRE = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_ ]*?)\s*:\s*(.*?)\s*(#.*)?$")


def key_of(line):
    m = KEYRE.match(line)
    if not m:
        return None, None
    return " ".join(m.group(1).upper().split()), m.group(2)


def main():
    raw = open(SRC, encoding="utf-8").read().split("\n")
    first = next(i for i, l in enumerate(raw) if l.strip().startswith("["))
    head, body = raw[:first], raw[first:]

    # --- the DEFAULT values from the global section (the authority) ---
    defaults = {}
    for l in head:
        s = l.strip()
        if s.startswith("DEFAULT ") and ":" in s:
            k, v = s[len("DEFAULT "):].split(":", 1)
            defaults[" ".join(k.upper().split())] = v.split("#")[0].strip()

    # --- split the body into profile blocks ---
    blocks, cur = [], None
    for l in body:
        if l.strip().startswith("["):
            cur = {"hdr": l, "lines": []}
            blocks.append(cur)
        else:
            if cur is None:
                cur = {"hdr": None, "lines": []}
                blocks.append(cur)
            cur["lines"].append(l)

    changes = []   # (profile, key, old, new, reason)
    out = list(head)
    for b in blocks:
        if b["hdr"] is None:
            out += b["lines"]
            continue
        name = re.match(r"^\[\s*(.*?)\s*\]", b["hdr"]).group(1).split("#")[0].strip()
        lines = list(b["lines"])
        kv = {}
        last_key_idx = -1
        for i, l in enumerate(lines):
            k, v = key_of(l)
            if k:
                kv[k] = (i, v)
                last_key_idx = i
        caid = kv.get("CAID", (None, ""))[1]

        # Rule 2: timing. Only lines that exist are rewritten.
        new_to = None
        if caid in MEASURED_TIMEOUT:
            new_to = MEASURED_TIMEOUT[caid]
            reason = "measured: decode log 917/948/1040 ms"
        elif ("SERVER TIMEOUT" in kv and kv["SERVER TIMEOUT"][1] == str(STOCK_TIMEOUT_FROM)
              and not ("DCW TIMEOUT" in kv and int(kv["DCW TIMEOUT"][1]) <= DEFAULT_TIMEOUT)):
            # DCW TIMEOUT must exceed SERVER TIMEOUT (cfgcheck). A profile whose DCW
            # TIMEOUT is not above 1000 keeps its 800: raising it would break the rule.
            new_to = DEFAULT_TIMEOUT
            reason = "800 -> file default 1000 (small-sample class)"
        if new_to is not None:
            for key in ("SERVER TIMEOUT", "SERVER VALIDECMTIME"):
                if key in kv and kv[key][1] in (str(STOCK_TIMEOUT_FROM), str(DEFAULT_TIMEOUT), "1200"):
                    i, old = kv[key]
                    if old != str(new_to):
                        indent = re.match(r"^(\s*)", lines[i]).group(1)
                        lines[i] = f"{indent}{key}: {new_to}"
                        changes.append((name, key, old, str(new_to), reason))

        # Rule 1: make inherited keys explicit (value taken from DEFAULT).
        add = []
        for key in INHERITABLE:
            if key in kv or key not in defaults:
                continue
            add.append(f"{key}: {defaults[key]}")
            changes.append((name, key, "(inherited)", defaults[key], "explicit, same value"))
        if add:
            pos = last_key_idx + 1 if last_key_idx >= 0 else len(lines)
            lines[pos:pos] = add

        out.append(b["hdr"])
        out += lines

    banner = [
        "# ============================================================================",
        "# CANDIDATE built by tests/fresh_cfg.py (R56) from multics-r24-optimized.cfg.",
        "# NOT the production file. Same behaviour, except the timing rule documented in",
        "# docs/FRESH-CONFIG-R56-ar.md. Do not start production with this file until it",
        "# is promoted by a decision.",
        "# ============================================================================",
    ]
    text = "\n".join(banner + out)
    if not text.endswith("\n"):
        text += "\n"
    open(DST, "w", encoding="utf-8").write(text)

    print(f"wrote {DST}")
    print(f"changes: {len(changes)}")
    by = {}
    for c in changes:
        by.setdefault((c[1], c[4]), 0)
        by[(c[1], c[4])] += 1
    for (k, why), n in sorted(by.items(), key=lambda x: (x[0][1], x[0][0])):
        print(f"  {n:3d}  {k:24s} {why}")
    if "--csv" in sys.argv:
        for c in changes:
            print("CHANGE|" + "|".join(c))


if __name__ == "__main__":
    main()
