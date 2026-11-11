#!/usr/bin/env python3
# cfgcheck.py -- TASK R40 (D103): static audit of the shipped profiles file.
#
# WHY THIS EXISTS. R40 normalized multics-r24-optimized.cfg per CA family
# (src/casystem.h). Every rule below is a property that was WRONG or DEAD in
# the file before that round, so the check is the regression net: a future
# edit that reintroduces a dead key, a mis-applied per-profile key, or a
# family-inappropriate value fails here in under a second -- before the
# slower prof/qv targets would even boot the server.
#
# It reads the file only; it never starts anything.
#
#   python3 cfgcheck.py ../multics-r24-optimized.cfg
import sys, re, collections

DEAD_GLOBAL = {
    "DEFAULT FILTER", "DEFAULT FILTER TIME", "FILTER", "FILTER TIME", "DEFAULT ECM CHECK",
    "DEFAULT ECM CHECK LENGTH", "DEFAULT DCW VALIDECMTIME",
    "DEFAULT ENABLE MGCAMD", "DEFAULT ENABLE FREECCCAM",
}
DEAD_PROFILE = {
    "DCW VALIDECMTIME",     # no parser branch anywhere (SERVER VALIDECMTIME is the real key)
    "CACHEEX PUSH",         # no parser branch
    "CACHEEX RELAY",        # no parser branch
    "FILTER", "FILTER TIME", # no top-level parser branches
    "CACHE FILTER", "CACHE FILTER TIME", # global cache fields, even inside a profile
    "ACCEPT NULL ONID",     # needs profile context but mutates global cache flag
    "ENABLE CAMD35", "ENABLE CS378X", "ENABLE MGCAMD", # no profile ENABLE branches
}
FIXED_CAIDS = {0x2600, 0x2610, 0x1010}
ICAM = {0x0963, 0x098C, 0x098D, 0x098E, 0x09C4, 0x09CD}


def key_of(line):
    line = line.strip()
    if not line or line.startswith("#") or ":" not in line:
        return None
    return line.split(":", 1)[0].strip().upper()


def parse(path):
    glob = collections.OrderedDict()
    profs = []
    cur = None
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        if line.startswith("["):
            cur = {"name": line[1:].split("]")[0], "line": n,
                   "kv": collections.OrderedDict(), "keys": []}
            profs.append(cur)
            continue
        k = key_of(line)
        if not k:
            continue
        v = line.split(":", 1)[1].strip()
        if cur is None:
            glob.setdefault(k, []).append(v)
        else:
            cur["kv"].setdefault(k, []).append(v)
            cur["keys"].append(k)
    return glob, profs


def _valid_line_record(key, values):
    # Mirror only the required leading fields of config.c's N/C/L/R parser;
    # a key with no host/port is not a configured line anchor. F is a local
    # CCcam account, not an outbound line.
    for value in values:
        fields = value.split("#", 1)[0].split()
        if len(fields) < 2 or not fields[0]:
            continue
        if key == "N":
            specs = fields[1].split(",")
            valid = True
            for spec in specs:
                bounds = spec.split(":", 1)
                if len(bounds) > 2 or not all(part.isdigit() for part in bounds):
                    valid = False
                    break
                ports = [int(part) for part in bounds]
                if any(port < 1 or port > 65535 for port in ports):
                    valid = False
                    break
                if len(ports) == 2 and ports[0] > ports[1]:
                    valid = False
                    break
            if valid and len(fields) >= 4:
                return True
        elif key in {"C", "L"}:
            if (fields[1].isdigit() and 1 <= int(fields[1]) <= 65535
                    and len(fields) >= 4):
                return True
        elif key == "R":
            if (fields[1].isdigit() and 1 <= int(fields[1]) <= 65535
                    and len(fields) >= 3
                    and re.fullmatch(r"(?:0[xX])?[0-9A-Fa-f]{1,4}", fields[2])):
                return True
    return False


def has_outbound_line_definition(glob, profs):
    line_keys = {"N", "C", "L", "R"}
    for key in line_keys:
        if key in glob and _valid_line_record(key, glob[key]):
            return True
    for profile in profs:
        for key in line_keys:
            if key in profile["kv"] and _valid_line_record(key, profile["kv"][key]):
                return True
    return False


TIMEOUT_LIMITS = {
    "CACHE TIMEOUT": (0, 5000),
    "SERVER TIMEOUT": (300, 10000),
}


def valid_timeout_value(key, value):
    limits = TIMEOUT_LIMITS.get(key)
    return (limits is not None and isinstance(value, int)
            and limits[0] <= value <= limits[1])


def main(path):
    glob, profs = parse(path)
    fails = []
    oks = []
    warnings = []

    def ok(msg):
        oks.append(msg)

    def bad(msg):
        fails.append(msg)

    def warn(msg):
        warnings.append(msg)

    if not has_outbound_line_definition(glob, profs):
        warn("blind configuration: no outbound line-server records (N/C/L/R); cache-only profiles have no configured line anchor")

    # ---- global level ----
    for k in DEAD_GLOBAL:
        if k in glob:
            bad("global dead key present: %s (no parser branch)" % k)
    for k in ("CACHE FILTER", "CACHE FILTER TIME",
              "CAID FAMILY PROVIDERCHECK", "CAID FAMILY NOPROVCHK",
              "DEFAULT ENABLE CAMD35", "DEFAULT ENABLE CS378X"):
        if k not in glob:
            bad("global key missing: %s" % k)
    # The parser skips ACCEPT NULL ONID before any profile, and the cache
    # checks using the resulting global field are currently commented out.
    if "ACCEPT NULL ONID" in glob:
        bad("global ACCEPT NULL ONID is skipped by the parser")
    if glob.get("CAID FAMILY HALFNULLED", [""])[0].replace(" ", "") != "09":
        bad("CAID FAMILY HALFNULLED must be exactly 09 (NDS)")
    if not fails:
        ok("global section: no dead DEFAULT keys; CACHE FILTER/family policy explicit; no inert global ONID")

    # R48 D-2 is opt-in. Keep both shipped baseline configs behavior-neutral:
    # an omitted key means OFF, while an explicit value must be a parser token.
    boolean_tokens = {"0", "1", "NO", "YES", "OFF", "ON"}
    evidence_keys = ("DEFAULT CACHE EVIDENCE", "CACHE EVIDENCE")
    for key in evidence_keys:
        for value in glob.get(key, []):
            token = value.split("#", 1)[0].strip().upper()
            if token not in boolean_tokens:
                bad("global %s has invalid boolean %r" % (key, value))
            elif token in {"1", "YES", "ON"}:
                bad("global %s must remain OFF in the shipped config" % key)
    for p in profs:
        for value in p["kv"].get("CACHE EVIDENCE", []):
            token = value.split("#", 1)[0].strip().upper()
            if token not in boolean_tokens:
                bad("%s: CACHE EVIDENCE has invalid boolean %r" % (p["name"], value))
            elif token in {"1", "YES", "ON"}:
                bad("%s: CACHE EVIDENCE must remain OFF in the shipped config" % p["name"])
    if not fails:
        ok("R48 CACHE EVIDENCE: opt-in remains OFF in the shipped profile file")

    # ---- profiles ----
    if len(profs) != 72:
        bad("expected 72 profiles, found %d" % len(profs))
    ports = collections.Counter()
    for p in profs:
        name = p["name"]
        kv = p["kv"]
        for k in ("PORT", "CAID", "PROVIDERS", "CA MODERN", "DCW CHECK",
                  "DCW HALFNULLED", "CACHE TIMEOUT", "SERVER TIMEOUT"):
            if k not in kv:
                bad("%s: missing %s" % (name, k))
        for k, values in kv.items():
            if len(values) > 1:
                bad("%s: duplicate key %s" % (name, k))
        # Match config.c's distinct runtime ranges instead of conflating the
        # 0..5000 CACHE TIMEOUT clamp with SERVER TIMEOUT's 300..10000 range.
        # A1: a nonzero per-profile value is the operator's effective timeout
        # and must not be overwritten by a CAID preset (parser precedence is
        # covered by the production C regression).
        timeout_values = {}
        for timeout_key, (minimum, maximum) in TIMEOUT_LIMITS.items():
            try:
                timeout_text = kv[timeout_key][0].split("#", 1)[0].strip()
                timeout_values[timeout_key] = int(timeout_text, 10)
                if not valid_timeout_value(timeout_key, timeout_values[timeout_key]):
                    bad("%s: %s must be in [%d, %d] ms" %
                        (name, timeout_key, minimum, maximum))
            except (KeyError, IndexError, ValueError):
                bad("%s: %s must be an integer in [%d, %d] ms" %
                    (name, timeout_key, minimum, maximum))
        for k in DEAD_PROFILE:
            if k in kv:
                bad("%s: dead/global-only key inside a profile: %s" % (name, k))
        if "CACHEEX MAXHOP" in kv:
            bad("%s: CACHEEX MAXHOP clamps to 2 -- use CACHEEX MAXHOP_LG" % name)
        try:
            caid = int(kv["CAID"][0].split(",")[0].strip(), 16)
        except (KeyError, ValueError):
            continue
        if kv.get("PORT"):
            ports[kv["PORT"][0]] += 1
        if "," in kv["CAID"][0]:
            bad("%s: parser accepts only one CAID per profile" % name)
        if caid == 0x1814:
            # The two shipped MEO PT profiles deliberately override the
            # static 1814 preset (cache 280/server 900) with operator values
            # 400/1100. Do not normalize these overrides back to the preset.
            if timeout_values.get("CACHE TIMEOUT") != 400:
                bad("%s: CAID 1814 CACHE TIMEOUT must retain the explicit 400 ms A1 override" % name)
            if timeout_values.get("SERVER TIMEOUT") != 1100:
                bad("%s: CAID 1814 SERVER TIMEOUT must retain the explicit 1100 ms A1 override" % name)
        fam = caid >> 8
        fixed = caid in FIXED_CAIDS
        if not fixed:
            if kv.get("CA MODERN", [""])[0].upper() != "ON":
                bad("%s: CA MODERN must be ON" % name)
            if kv.get("DCW CHECK", [""])[0].upper() != "YES":
                bad("%s: DCW CHECK must be YES" % name)
            # TASK R42 (D105): the floor is opt-in and OFF in the shipped
            # file. 100 was armed in 70 profiles and threw away every
            # legitimate answer faster than 100 ms (measured: a 30 ms card
            # server scored 0/4 with it, 4/4 without). A future edit that
            # arms it again fails here.
            if kv.get("DCW MINTIME", [""])[0] != "0":
                bad("%s: DCW MINTIME must be 0 (opt-in off, D105)" % name)
            if "SERVER VALIDECMTIME" not in kv:
                bad("%s: SERVER VALIDECMTIME missing" % name)
            if kv.get("ENABLE CACHEEX", [""])[0].upper() != "YES":
                bad("%s: ENABLE CACHEEX must be YES for a CA profile" % name)
        if fam == 0x09 and caid not in ICAM:
            if kv.get("DCW HALFNULLED", [""])[0].upper() != "YES":
                bad("%s: VideoGuard CSA (09xx) needs DCW HALFNULLED: YES" % name)
            if kv.get("DCW SWAP", [""])[0].upper() != "YES":
                bad("%s: VideoGuard CSA (09xx) needs DCW SWAP: YES" % name)
        elif caid in ICAM:
            if kv.get("DCW HALFNULLED", [""])[0].upper() != "NO":
                bad("%s: iCam AES CW is never half-nulled -- HALFNULLED must be NO" % name)
            if "DCW SWAP" in kv:
                bad("%s: iCam AES profile must not carry DCW SWAP" % name)
        else:
            if kv.get("DCW HALFNULLED", [""])[0].upper() != "NO":
                bad("%s: family %02X must not allow half-nulled keys" % (name, fam))
            if "DCW SWAP" in kv:
                bad("%s: DCW SWAP is inert outside family 09 -- remove it" % name)
        if fixed:
            if kv.get("ENABLE CACHE", [""])[0].upper() != "NO":
                bad("%s: fixed-key profile must set ENABLE CACHE: NO" % name)
            if kv.get("ENABLE CACHEEX", [""])[0].upper() != "NO":
                bad("%s: fixed-key profile must set ENABLE CACHEEX: NO" % name)
            if kv.get("ENABLE SOFTCAM", [""])[0].upper() != "YES":
                bad("%s: fixed-key profile must set ENABLE SOFTCAM: YES" % name)
            if caid in (0x2600, 0x1010) and \
               kv.get("DCWFILTER PATTERN", [""])[0].upper() != "OFF":
                bad("%s: a repeated 8-byte key trips DCWFILTER PATTERN -- set it OFF" % name)
            if "DCW MINTIME" in kv:
                bad("%s: fixed-key profile must not carry DCW MINTIME" % name)
    for port, n in ports.items():
        if n > 1:
            bad("duplicate PORT %s in %d profiles" % (port, n))
    if len(ports) == len(profs):
        ok("72 profiles: unique ports, one family block each")

    # ---- summary ----
    for message in warnings:
        print("  [warn] " + message)
    for f in fails:
        print("  [FAIL] " + f)
    if fails:
        print("cfgcheck: %d failed, %d passed" % (len(fails), len(oks)))
        return 1
    for message in oks:
        print("  [ ok ] " + message)
    print("  [ ok ] known dead keys: none (global or per-profile)")
    print("  [ ok ] configured family policy: 09xx half+swap, iCam no half, fixed keys local")
    print("cfgcheck: %d checks ok" % (len(oks) + 2))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: cfgcheck.py <profiles.cfg>")
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
