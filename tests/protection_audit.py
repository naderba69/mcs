#!/usr/bin/env python3
# protection_audit.py -- TASK R62: per-profile audit of the protection engines.
#
# WHY THIS EXISTS. Every profile of multics-r24-optimized.cfg carries its own
# protection settings, and each CA family needs a different set. Reading the
# file by eye cannot prove that each profile gets the value its family needs,
# after inheritance. This tool computes the EFFECTIVE value of every
# protection key for every profile, using the same order the parser uses:
#
#   1. the profile's own line                        (wins)
#   2. the DEFAULT line in the global section        (copied at profile start)
#   3. the built-in default in src/config.c          (init_cardserver)
#
# Then it checks the effective values against the family rules below.
#
# It reads the file only, unless --live is given. With --live it also starts
# nothing: it asks an already running server for /profiles and /profile?id=N,
# and compares the values the server SHOWS with the values the file implies.
# The profile page does not show every protection key; those are reported as
# "not shown" and are audited from the file only, never called verified live.
#
#   python3 protection_audit.py ../multics-r24-optimized.cfg
#   python3 protection_audit.py ../multics-r24-optimized.cfg --live http://127.0.0.1:PORT user:pass
#
# Exit status: 0 when no rule is violated, 1 otherwise.
import collections
import re
import sys
import base64
import urllib.request

# ---------------------------------------------------------------- code defaults
# Values that init_cardserver() (src/config.c) and the parser give when neither
# a profile line nor a DEFAULT line sets the key. Each entry says where it comes
# from, so a reviewer can check it against the code.
CODE_DEFAULT = {
    "DCW CHECK": "NO",                 # init_cardserver: dcw.check = 0
    "DCW CYCLE_CHECK": "NO",           # dcwfilter_cycle: zero-initialised
    "ENABLE SKIPCWC": "NO",            # defaultcs.option.skipcwc: zero-initialised
    "CA MODERN": "NO",                 # cardserver->option.ca_modern: zero-initialised
    "DCW HALFNULLED": "NO",            # dcw.halfnulled: zero-initialised
    "DCW SWAP": "NO",                  # dcw.swap: zero-initialised
    "DCW MAXFAILED": "0",              # maxfailedecm: zero-initialised
    "DCW MINTIME": "0",                # dcw.mintime: zero-initialised
    "DCW VALIDECMTIME": "0",           # init_cardserver / dcw.validecmtime
    "ECM CHECK": "NO",                 # option.checkecm: zero-initialised
    "ECM CHECK LENGTH": "YES",         # init_cardserver: checkecmlength = 1
    "ENABLE ECMLOOPING": "NO",         # option.ecmlooping: zero-initialised
    "DCWFILTER PATTERN": "NO",         # dcwfilter_pattern: zero-initialised
    "CACHEEX BLOCK_FAKE_CW": "NO",     # init_cardserver: fexblockfake = 0
    "CACHEEX CWCHECK": "0",            # init_cardserver: excheck = 0
    "CACHEEX LOCAL_ONLY": "NO",        # init_cardserver: fexlocalonly = 0
    "ENABLE CACHEEX": "NO",            # init_cardserver: fallowcacheex = 0
    "ENABLE CACHE": "YES",             # init_cardserver: fallowcache = 1
    "ACCEPT NULL PROVIDER": "YES",     # init_cardserver: faccept0provider = 1
    "CACHE STATIC": "NO",
}

# Protection keys the audit reports. (key, where it may be set)
ENGINE_KEYS = [
    "CA MODERN",
    "DCW CHECK",
    "DCW HALFNULLED",
    "DCW SWAP",
    "DCW CYCLE_CHECK",
    "DCWFILTER PATTERN",
    "ENABLE SKIPCWC",
    "DCW MINTIME",
    "DCW VALIDECMTIME",
    "DCW MAXFAILED",
    "ECM CHECK",
    "ECM CHECK LENGTH",
    "ENABLE ECMLOOPING",
    "ENABLE CACHE",
    "ENABLE CACHEEX",
    "CACHE STATIC",
    "CACHE TIMEOUT",
    "CACHEEX BLOCK_FAKE_CW",
    "CACHEEX CWCHECK",
    "CACHEEX LOCAL_ONLY",
]

# Keys the profile page (/profile?id=N) prints, with the label it uses.
# Keys not listed here cannot be checked live.
PAGE_LABEL = {
    "ECM CHECK": "ECM CHECK",
    "ECM CHECK LENGTH": "ECM CHECK LENGTH",
    "DCW CHECK": "DCW CHECK",
    "DCW HALFNULLED": "DCW HALFNULLED",
    "DCW SWAP": "DCW SWAP",
    "DCW MAXFAILED": "DCW MAXFAILED",
    "DCW VALIDECMTIME": "DCW VALIDECMTIME",
    "ENABLE ECMLOOPING": "ENABLE ECMLOOPING",
    "ENABLE CACHE": "ENABLE CACHE",
    "ENABLE CACHEEX": "ENABLE CACHEEX",
    "CACHE STATIC": "CACHE STATIC",
    "CACHE TIMEOUT": "CACHE TIMEOUT",
}

# ---------------------------------------------------------------- families
# Family of a profile, from its CAID, using src/casystem.h. The exact iCam
# list is the one casystem.h marks CA_F_ICAM.
ICAM = {0x0963, 0x098C, 0x098D, 0x098E, 0x09C4, 0x09CD}
CONST_CW = {0x2600}              # BISS1: fixed local keys (CA_CW_CONST)
AES_CW = {0x2610, 0x5601, 0x5600}  # BISS2, Verimatrix: no checksum CW
FAMILY_BY_HIGH = {
    0x01: "Seca", 0x05: "Viaccess", 0x06: "Irdeto", 0x09: "VideoGuard",
    0x0B: "Conax", 0x0D: "CryptoWorks", 0x0E: "PowerVu", 0x17: "BetaCrypt",
    0x18: "Nagravision", 0x4A: "DRE-Crypt", 0x26: "BISS", 0x10: "Tandberg",
    0x56: "Verimatrix",
}


def family_of(caid):
    if caid in ICAM:
        return "VideoGuard iCam", "AES"
    if caid in CONST_CW:
        return "BISS1 (fixed key)", "CONST"
    if caid in AES_CW:
        return FAMILY_BY_HIGH.get(caid >> 8, "AES family"), "AES"
    if caid == 0x1010:
        return "Tandberg (fixed key)", "CONST"
    return FAMILY_BY_HIGH.get(caid >> 8, "unknown (treated as CSA)"), "CSA"


# ---------------------------------------------------------------- parsing
def parse(path):
    """Return (defaults, profiles). defaults: DEFAULT/global keys, last wins.
    profiles: list of dicts with name, line, kv (own keys)."""
    defaults = {}
    profiles = []
    cur = None
    with open(path, encoding="utf-8", errors="replace") as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.rstrip("\n")
            if line.startswith("["):
                name = line[1:].split("]", 1)[0].strip()
                cur = {"name": name, "line": n, "kv": collections.OrderedDict()}
                profiles.append(cur)
                continue
            s = line.strip()
            if not s or s.startswith("#") or ":" not in s:
                continue
            key, val = s.split(":", 1)
            key = re.sub(r"\s+", " ", key.strip().upper())
            val = val.split("#", 1)[0].strip()
            if cur is None:
                if key.startswith("DEFAULT "):
                    defaults[key[len("DEFAULT "):]] = val
                else:
                    defaults[key] = val
            else:
                cur["kv"][key] = val
    return defaults, profiles


def effective(defaults, prof, key):
    """Effective value of key for a profile, and where it came from."""
    if key in prof["kv"]:
        return prof["kv"][key], "profile"
    if key in defaults:
        return defaults[key], "DEFAULT"
    if key == "DCWFILTER PATTERN":
        # dcw_filter_pattern is global; CACHE PROTECTION: STANDARD arms it
        # (config.c:3881). A profile that does not set the key inherits it.
        armed = defaults.get("CACHE PROTECTION", "").strip().upper() == "STANDARD"
        return ("YES" if armed else "NO"), "global CACHE PROTECTION"
    return CODE_DEFAULT.get(key, "?"), "code"


def yn(v):
    v = str(v).strip().upper()
    if v in ("YES", "ON", "1"):
        return "YES"
    if v in ("NO", "OFF", "0"):
        return "NO"
    return v


# ---------------------------------------------------------------- rules
def expectations(caid, family, kind):
    """Rules per family. Each returns (key, expected, why). The expected value
    comes from the family itself (casystem.h) and from the file's own header
    comments (R40). A key that must not depend on the family is "any"."""
    rules = []
    # Universal: the modern CA layer is on for every profile of the file.
    rules.append(("CA MODERN", "YES", "one CA layer for every family"))
    rules.append(("DCW CHECK", "YES", "structural checksum gate"))
    rules.append(("DCW CYCLE_CHECK", "YES", "cycle gate at delivery"))
    rules.append(("ENABLE SKIPCWC", "YES", "key validity test before send"))
    rules.append(("DCW MINTIME", "0", "R42/D105: a fast honest card is not refused"))
    rules.append(("ECM CHECK LENGTH", "YES", "ECM length whitelist"))
    if kind == "CSA" and family.startswith("VideoGuard"):
        rules.append(("DCW HALFNULLED", "YES", "VideoGuard CSA half-null keys are legal"))
        rules.append(("DCW SWAP", "YES", "try the swapped halves"))
    elif kind == "CSA":
        rules.append(("DCW HALFNULLED", "NO", "half-null is not legal for this family"))
    elif kind == "AES" and family.startswith("VideoGuard"):
        rules.append(("DCW HALFNULLED", "NO", "iCam key is 16 bytes, no half-null"))
    if kind != "CONST":
        rules.append(("DCWFILTER PATTERN", "YES", "fake-shape gate armed (CACHE PROTECTION: STANDARD)"))
    if kind == "CONST":
        rules.append(("ENABLE CACHE", "NO", "fixed local key must not be stored"))
        rules.append(("ENABLE CACHEEX", "NO", "fixed local key must not be shared"))
        rules.append(("DCWFILTER PATTERN", "NO", "both halves equal: pattern gate refuses it"))
    return rules


# ---------------------------------------------------------------- main
def audit(path, live=None):
    defaults, profiles = parse(path)
    rows = []
    violations = []
    unexplained = []
    dead_lists = []
    for p in profiles:
        caid_list = [c.strip() for c in effective(defaults, p, "CAID")[0].split(",") if c.strip()]
        try:
            caid = int(caid_list[0], 16)
        except (ValueError, IndexError):
            caid = 0
        family, kind = family_of(caid)
        vals = {}
        src = {}
        for k in ENGINE_KEYS:
            v, s = effective(defaults, p, k)
            vals[k] = v
            src[k] = s
        for key, want, why in expectations(caid, family, kind):
            got = vals.get(key)
            if key not in vals:
                v, s = effective(defaults, p, key)
                got = v
            if key in ("DCW CHECK", "DCW HALFNULLED", "DCW SWAP", "DCW CYCLE_CHECK",
                       "ENABLE SKIPCWC", "CA MODERN", "ECM CHECK LENGTH",
                       "ENABLE CACHE", "ENABLE CACHEEX", "DCWFILTER PATTERN"):
                ok = yn(got) == want
            else:
                ok = str(got).strip() == want
            if not ok:
                violations.append((p["name"], caid, key, got, want, why))
        if vals["ECM CHECK LENGTH"] != "YES" and src["ECM CHECK LENGTH"] == "profile":
            unexplained.append((p["name"], "ECM CHECK LENGTH", vals["ECM CHECK LENGTH"]))
        if "ECM LENGTH" in p["kv"] and yn(vals["ECM CHECK LENGTH"]) == "NO":
            dead_lists.append((p["name"], p["kv"]["ECM LENGTH"]))
        rows.append((p, caid, family, kind, vals, src))
    audit.dead_lists = dead_lists
    return rows, violations, unexplained


def live_compare(rows, live_base, auth):
    """Compare the values /profile?id=N shows with the file-derived ones."""
    req = urllib.request.Request(live_base + "/profiles")
    req.add_header("Authorization", "Basic " + base64.b64encode(auth.encode()).decode())
    html = urllib.request.urlopen(req, timeout=10).read().decode("utf-8", "replace")
    ids = {}
    for m in re.finditer(r'profile\?id=(\d+)">([^<]+)<', html):
        ids.setdefault(m.group(2).strip(), int(m.group(1)))
    mismatch = []
    shown = 0
    for p, caid, family, kind, vals, src in rows:
        pid = ids.get(p["name"])
        if pid is None:
            mismatch.append((p["name"], "-", "profile not listed by the server", "", ""))
            continue
        req = urllib.request.Request(live_base + "/profile?id=%d" % pid)
        req.add_header("Authorization", "Basic " + base64.b64encode(auth.encode()).decode())
        page = urllib.request.urlopen(req, timeout=10).read().decode("utf-8", "replace")
        text = re.sub(r"<[^>]+>", "\n", page)
        lines = [x.strip() for x in text.split("\n") if x.strip()]
        table = {}
        for i, x in enumerate(lines[:-1]):
            table.setdefault(x, lines[i + 1])
        for key, label in PAGE_LABEL.items():
            if label not in table:
                continue
            shown += 1
            shown_v = table[label]
            want = vals[key]
            if key in ("DCW CHECK", "DCW HALFNULLED", "DCW SWAP", "ECM CHECK", "ECM CHECK LENGTH",
                       "ENABLE ECMLOOPING", "ENABLE CACHE", "ENABLE CACHEEX", "CACHE STATIC"):
                a, b = yn(shown_v), yn(want)
            else:
                a = re.sub(r"[^0-9]", "", shown_v)
                b = re.sub(r"[^0-9]", "", want)
            if a != b:
                mismatch.append((p["name"], key, "page shows " + shown_v, "file implies " + want, ""))
    return shown, mismatch


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    rows, violations, unexplained = audit(sys.argv[1])
    print("| # | profile | CAID | family | CA MODERN | HALFNULLED | SWAP | CYCLE | PATTERN | SKIPCWC | MINTIME | VALIDECMTIME | MAXFAILED | ECM CHECK | ECM LEN | LOOPING | CACHE STATIC | CW-CHECK | BLOCK_FAKE | LOCAL_ONLY |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for i, (p, caid, family, kind, vals, src) in enumerate(rows, 1):
        cells = [
            str(i), p["name"], "%04X" % caid, family,
            yn(vals["CA MODERN"]), yn(vals["DCW HALFNULLED"]), yn(vals["DCW SWAP"]),
            yn(vals["DCW CYCLE_CHECK"]), yn(vals["DCWFILTER PATTERN"]), yn(vals["ENABLE SKIPCWC"]),
            vals["DCW MINTIME"], vals["DCW VALIDECMTIME"], vals["DCW MAXFAILED"],
            yn(vals["ECM CHECK"]), yn(vals["ECM CHECK LENGTH"]), yn(vals["ENABLE ECMLOOPING"]),
            yn(vals["CACHE STATIC"]), vals["CACHEEX CWCHECK"],
            yn(vals["CACHEEX BLOCK_FAKE_CW"]), yn(vals["CACHEEX LOCAL_ONLY"]),
        ]
        print("| " + " | ".join(cells) + " |")
    print()
    print("violations: %d" % len(violations))
    for v in violations:
        print("  VIOLATION %s caid=%04X %s: effective=%s expected=%s (%s)" % (v[0], v[1], v[2], v[3], v[4], v[5]))
    print("ECM LENGTH whitelists that are never enforced (check is NO): %d" % len(audit.dead_lists))
    for d in audit.dead_lists:
        print("  %s ECM LENGTH=%s" % d)
    print("profiles with ECM CHECK LENGTH overridden to NO: %d" % len(unexplained))
    for u in unexplained:
        print("  %s %s=%s" % u)
    if len(sys.argv) >= 5 and sys.argv[2] == "--live":
        live_base, auth = sys.argv[3], sys.argv[4]
        shown, mism = live_compare(rows, live_base, auth)
        print("live: %d page values compared, %d mismatches" % (shown, len(mism)))
        for m in mism:
            print("  MISMATCH %s %s: %s / %s" % m[:4])
        if mism:
            violations.extend(mism)
    sys.exit(1 if violations else 0)
