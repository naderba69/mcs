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

# Rule 4 (R57): feature knobs written explicitly in every profile. The value is
# the one the code already gives when the key is absent (init_cardserver() in
# config.c memsets to 0 and then sets checkecmlength=1; ECMLOOPING inherits
# DEFAULT YES). Writing them changes nothing; it makes each profile state them.
# DCW VALIDECMTIME stays 0 (OFF): it refuses late keys, and no per-CAID reply
# time is measured yet, so arming it would be a guess (docs/FRESH-CONFIG-R57-ar.md).
FEATURE_KNOBS = [
    ("ENABLE ECMLOOPING", "YES"),
    ("DCW MINTIME", "0"),
    ("DCW VALIDECMTIME", "0"),
    ("ECM CHECK LENGTH", "YES"),   # profiles that say NO keep NO (not overwritten)
]
CODE_DEFAULT = {"DCW MINTIME": "0", "DCW VALIDECMTIME": "0", "ECM CHECK LENGTH": "YES"}

# Measured window overrides (CAID -> server timeout ms). Source: docs/FRESH-CONFIG-R56-ar.md.
# R58: CAID 1814 (MEO PT) measured on the live channel 1814:005211:13AE. Times are
# from the ECM's arrival: the first server was sent at 263 ms (cache wait of R57),
# and the three valid replies took 1209 / 1054 / 713 ms after their own send.
# Fix: send at once (no cache wait), all three servers first, the minimum interval,
# and a server window (routing exclusion) above the slowest measured reply.
MEASURED_PROFILE = {
    "1814": {
        "SERVER FIRST": "3",          # was 2
        "SERVER INTERVAL": "100",     # was 150 (100 = the minimum the parser allows)
        "SERVER TIMEOUT": "1500",     # was 1200: sending window, above all sends
        "SERVER VALIDECMTIME": "1500",# was 1200: slowest measured average 1209 -> 1500
        "CACHE STATIC": "NO",         # was YES (R57): the cache wait delayed the first send by 250 ms
    },
}
MEASURED_TIMEOUT = {}   # superseded by MEASURED_PROFILE (kept for the timing branch below)
FIXED_CAIDS = {"2600", "2610", "1010"}   # BISS1, BISS2, Tandberg: fixed local keys
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

        # Rule 5 (R57): cache first. CACHE STATIC: YES makes the ECM wait for the
        # cache before the server is asked (th-ecm.c:119-140). The wait is bounded by
        # CACHE TIMEOUT (250 ms), and it is paid on every ECM the cache does not hold.
        # Fixed-key profiles keep ENABLE CACHE: NO, so they stay NO here.
        static_yes_line = False
        if caid in MEASURED_PROFILE:
            for key, val in MEASURED_PROFILE[caid].items():
                assert key in kv, f"{name}: {key} must be explicit for a measured profile"
                i, old = kv[key]
                if old != val:
                    indent = re.match(r"^(\s*)", lines[i]).group(1)
                    lines[i] = f"{indent}{key}: {val}"
                    changes.append((name, key, old, val, "measured R58: live 1814:005211:13AE"))
                kv[key] = (i, val)
        if caid not in FIXED_CAIDS and caid not in MEASURED_PROFILE:
            if "CACHE STATIC" in kv and kv["CACHE STATIC"][1].upper() == "NO":
                i, old = kv["CACHE STATIC"]
                indent = re.match(r"^(\s*)", lines[i]).group(1)
                lines[i] = f"{indent}CACHE STATIC: YES"
                changes.append((name, "CACHE STATIC", old, "YES", "cache first: armed (cost <= CACHE TIMEOUT)"))
            elif "CACHE STATIC" not in kv:
                # inherits DEFAULT NO: write YES as a new line (rule 1 must not add NO)
                kv["CACHE STATIC"] = (None, "YES")   # placeholder: stops rule 1 writing NO
                changes.append((name, "CACHE STATIC", "(inherited NO)", "YES", "cache first: armed (cost <= CACHE TIMEOUT)"))
                static_yes_line = True

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
        if static_yes_line:
            add.append("CACHE STATIC: YES")
        for key in INHERITABLE:
            if key in kv or key not in defaults:
                continue
            add.append(f"{key}: {defaults[key]}")
            changes.append((name, key, "(inherited)", defaults[key], "explicit, same value"))
        for key, val in FEATURE_KNOBS:
            if key in kv:
                continue          # never overwrite an operator's explicit choice
            if key == "DCW MINTIME" and caid in FIXED_CAIDS:
                continue          # cfgcheck: fixed-key profiles must not carry MINTIME
            add.append(f"{key}: {val}")
            changes.append((name, key, "(code default)", val, "explicit feature knob, same value"))
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
