#!/usr/bin/env python3
"""Band 67: effective DCW-timeout audit for MultiCS profile configs.

Why this exists (band 66 root cause): ecm_get_effective_timeout()
(th-ecm.c) lets the provider table in src/ecm-provider-config.h
silently cap a profile's client deadline at fallback_timeout
(e.g. 1000 ms for 1884:000000) unless the profile sets
`DCW TIMEOUT:` explicitly. Slow card lines then always answer
after the client was already failed - the channel "brings no CW"
on this build while stock copies (no table) open it.

For every profile in the given config this prints the EFFECTIVE
deadline and its source:
  explicit  - `DCW TIMEOUT:` in the profile (wins over the table)
  TABLE-CAP - no explicit value, provider-table fallback applies
  default   - no explicit value, no table entry (code default)

Exit code 1 if any TABLE-CAP profile is found, so it can gate
config changes. Usage: profile-timeout-audit.py [config ...]
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
CODE_DEFAULT_MS = 3000  # src/config.c: cs->option.dcw.timeout = 3000


def load_table():
    src = (ROOT / "src" / "ecm-provider-config.h").read_text()
    table = {}
    cur = None
    for line in src.splitlines():
        m = re.search(r"ADD_CONFIG\(0x([0-9A-Fa-f]+),", line)
        if m:
            cur = int(m.group(1), 16)
            continue
        m = re.search(r"fallback_timeout\s*=\s*(\d+)", line)
        if m and cur is not None:
            v = int(m.group(1))
            table[cur] = min(v, table.get(cur, v))
    return table


def audit(path, table):
    text = pathlib.Path(path).read_text()
    capped = 0
    profiles = 0
    for part in re.split(r"\n\[", text)[1:]:
        name = part.split("]")[0].strip()
        caid_m = re.search(r"\nCAID:\s*([0-9A-Fa-f]{4})", part)
        if not caid_m:
            continue
        profiles += 1
        caid = int(caid_m.group(1), 16)
        exp = re.search(r"\nDCW TIMEOUT:\s*(\d+)", part)
        if exp:
            eff, src = int(exp.group(1)), "explicit"
        elif caid in table:
            eff, src = table[caid], "TABLE-CAP"
            capped += 1
        else:
            eff, src = CODE_DEFAULT_MS, "default"
        flag = "  <-- silently capped" if src == "TABLE-CAP" else ""
        print(f"{name[:42]:42} CAID {caid:04X}  effective {eff:5} ms  ({src}){flag}")
        # Band 68 invariant: the cache wait must finish inside the
        # client deadline, or a cache miss starves the server path.
        ct = re.search(r"\nCACHE TIMEOUT:\s*(\d+)", part)
        if ct and int(ct.group(1)) >= eff:
            capped += 1
            print(f"{name[:42]:42} CAID {caid:04X}  CACHE TIMEOUT {ct.group(1)} >= DCW {eff}  <-- cache wait can never complete")
    print(f"-- {path}: {profiles} profiles, {capped} violation(s)")
    return capped


def main():
    table = load_table()
    paths = sys.argv[1:] or [
        str(ROOT / "multics-full-30w-52e.cfg"),
        str(ROOT / "multics-r24-optimized.cfg"),
    ]
    bad = sum(audit(p, table) for p in paths)
    if bad:
        print(f"profile-timeout-audit: {bad} profile(s) still silently capped")
        return 1
    print("profile-timeout-audit: no silently capped profiles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
