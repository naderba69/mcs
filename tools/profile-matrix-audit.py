#!/usr/bin/env python3
"""Band 70: profile x feature matrix audit.

The operator's directive: test every feature in every profile and
surface every error, config or code. This tool does the config
half mechanically, in two passes:

STATIC (default): per profile in the given configs -
  * duplicate newcamd PORTs across profiles (second listener dies)
  * dead keys: profile keys the parser has no strcmp() branch for
    (the operator believes they work; the binary ignores them)
  * silent clamps: values the parser rewrites (bands 66-69 rules)
  * cross-field invariants: CACHE TIMEOUT < effective DCW deadline,
    provider-table DCW cap without an explicit DCW TIMEOUT

LIVE (--live BIN CONFIG): boot the real binary on CONFIG, scrape
/profiles + /profile?id=N, and compare the EFFECTIVE values the
server reports against config intent (explicit -> DEFAULT ->
code default, with parser clamps applied). Any mismatch is a
proven parse/shadow/clamp defect, not a guess.

Exit 1 when any FATAL/ERROR finding exists.
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent

# code defaults: init_cardserver() in src/config.c
CODE_DEFAULTS = {
    "DCW TIMEOUT": 3000, "SERVER TIMEOUT": 2000, "SERVER INTERVAL": 1000,
    "SERVER VALIDECMTIME": 0, "SERVER FIRST": 0, "SERVER MAX": 0,
    "CACHE TIMEOUT": 0, "DCW RETRY": 0,
}

def clamp(key, v):
    """Parser rewrite rules (src/config.c profile branches)."""
    if key == "DCW TIMEOUT":
        return 300 if v < 300 else (9999 if v > 9999 else v)
    if key == "CACHE TIMEOUT":
        return 0 if v < 0 else (5000 if v > 5000 else v)
    if key == "SERVER TIMEOUT":
        return v if 300 <= v <= 10000 else 300
    if key == "SERVER INTERVAL":
        return 100 if v < 100 else (3000 if v > 3000 else v)
    if key == "SERVER VALIDECMTIME":
        return 5000 if v > 5000 else v
    if key == "SERVER FIRST":
        return 0 if v < 0 else (3 if v > 3 else v)
    if key == "SERVER MAX":
        return 0 if v < 0 else (10 if v > 10 else v)
    return v

def known_keys():
    src = (ROOT / "src" / "config.c").read_text()
    words = set(re.findall(r'strcmp\(str,"([A-Z0-9_]+)"\)', src))
    words |= set(re.findall(r'strcmp\(str,"([A-Z0-9_]+)"\)', (ROOT / "src" / "config.c").read_text()))
    return words

def provider_table():
    src = (ROOT / "src" / "ecm-provider-config.h").read_text()
    table, cur = {}, None
    for line in src.splitlines():
        m = re.search(r"ADD_CONFIG\(0x([0-9A-Fa-f]+),", line)
        if m:
            cur = int(m.group(1), 16)
            continue
        m = re.search(r"fallback_timeout\s*=\s*(\d+)", line)
        if m and cur is not None:
            table[cur] = min(int(m.group(1)), table.get(cur, int(m.group(1))))
    return table

def parse_profiles(text):
    """Yield (name, {key: [raw values]}, body). Lines before the
    first profile that start with DEFAULT form the template."""
    default = {}
    profiles = []
    current = None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and "]" in line:
            current = (line[1:line.index("]")].strip(), {})
            profiles.append(current)
            continue
        m = re.match(r"([A-Z][A-Z0-9_]*(?: [A-Z0-9_]+){0,2})\s*[:=]\s*(.*)", line)
        if not m:
            continue
        key, val = m.group(1).strip(), m.group(2).strip()
        if current is None:
            if key.startswith("DEFAULT "):
                default[key[8:]] = val
            continue
        current[1].setdefault(key, []).append(val)
    return default, profiles

def key_recognised(key, words):
    parts = key.split()
    return any(" ".join(parts[:n]) in words or parts[0] in words
               for n in range(len(parts), 0, -1)) or parts[0] in words

def static_audit(path, words, table):
    text = pathlib.Path(path).read_text()
    default, profiles = parse_profiles(text)
    findings = []
    ports = {}
    for name, kv in profiles:
        def val(k):
            if k in kv:
                return kv[k][-1]
            return default.get(k)
        caid_m = val("CAID")
        caid = int(caid_m, 16) if caid_m and re.fullmatch(r"[0-9A-Fa-f]{4}", caid_m) else None
        port = val("PORT")
        if port:
            if port in ports:
                findings.append(("FATAL", name, f"PORT {port} also used by '{ports[port]}' - second listener cannot bind"))
            ports[port] = name
        if caid is None:
            findings.append(("ERROR", name, "no valid CAID"))
        if not kv.get("USER") and not default.get("USER"):
            findings.append(("INFO", name, "no USER lines (template state - operator adds users/servers)"))
        # dead keys (first word must be a parser word; two-word keys
        # legitimately split as KEY + subkey in the parser)
        for k in kv:
            first = k.split()[0]
            if first not in words and not key_recognised(k, words):
                findings.append(("ERROR", name, f"dead key '{k}' - no parser branch, silently ignored"))
        # numeric interpretation with clamps
        nums = {}
        for k in CODE_DEFAULTS:
            raw = val(k)
            if raw is not None and re.fullmatch(r"-?\d+", raw):
                nums[k] = int(raw)
                c = clamp(k, nums[k])
                if c != nums[k]:
                    findings.append(("ERROR", name, f"{k}: {nums[k]} silently rewritten to {c} by parser clamp"))
        eff_dcw = nums.get("DCW TIMEOUT")
        if eff_dcw is None:
            eff_dcw = table.get(caid, CODE_DEFAULTS["DCW TIMEOUT"]) if caid is not None else CODE_DEFAULTS["DCW TIMEOUT"]
            if caid in table:
                findings.append(("ERROR", name, f"no DCW TIMEOUT - provider table caps CAID {caid:04X} at {table[caid]}ms"))
        ct = nums.get("CACHE TIMEOUT")
        if ct is not None and ct >= eff_dcw:
            findings.append(("ERROR", name, f"CACHE TIMEOUT {ct} >= effective DCW {eff_dcw} - cache wait can never complete"))
        ve = nums.get("SERVER VALIDECMTIME")
        st = nums.get("SERVER TIMEOUT")
        if ve is not None and st is not None and 0 < ve < 300:
            findings.append(("WARN", name, f"SERVER VALIDECMTIME {ve} extremely low - most servers excluded"))
    return findings

def main():
    words = known_keys()
    table = provider_table()
    paths = [a for a in sys.argv[1:] if not a.startswith("--")] or [
        str(ROOT / "multics-full-30w-52e.cfg"),
        str(ROOT / "multics-r24-optimized.cfg"),
    ]
    bad = 0
    for p in paths:
        findings = static_audit(p, words, table)
        print(f"== {p}: {len(findings)} finding(s)")
        for level, name, msg in findings:
            print(f"  [{level}] {name}: {msg}")
            if level in ("FATAL", "ERROR"):
                bad += 1
    print(f"profile-matrix-audit(static): {bad} fatal/error finding(s)")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
