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

# TASK R45: "DCW VALIDECMTIME" is LIVE since R44 (D107, config.c 2533 and 4492),
# both at DEFAULT and per profile. It is not listed here any more. It stays
# absent from the shipped file on purpose: 0 (off) is the opt-in default.
DEAD_GLOBAL = {
    "DEFAULT FILTER", "DEFAULT FILTER TIME", "DEFAULT ECM CHECK",
    "DEFAULT ECM CHECK LENGTH",
}
DEAD_PROFILE = {
    "CACHEEX PUSH",         # no parser branch
    "CACHEEX RELAY",        # no parser branch
    "FILTER", "FILTER TIME",# global keys; a per-profile line silently rewrites the global
    "ACCEPT NULL ONID",     # global cache flag; same problem
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


def main(path):
    glob, profs = parse(path)
    fails = []
    oks = []

    def ok(msg):
        oks.append(msg)

    def bad(msg):
        fails.append(msg)

    # ---- global level ----
    for k in DEAD_GLOBAL:
        if k in glob:
            bad("global dead key present: %s (no parser branch)" % k)
    for k in ("FILTER", "FILTER TIME", "ACCEPT NULL ONID",
              "CAID FAMILY PROVIDERCHECK", "CAID FAMILY NOPROVCHK"):
        if k not in glob:
            bad("global key missing: %s" % k)
    if glob.get("CAID FAMILY HALFNULLED", [""])[0].replace(" ", "") != "09":
        bad("CAID FAMILY HALFNULLED must be exactly 09 (NDS)")
    if not fails:
        ok("global section: no dead DEFAULT keys; FILTER/ONID/family policy explicit")

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
    # ---- TASK R45 (D108): per-speed timeouts + ECMLOOPING ----
    fails_before_r45 = len(fails)
    # The "بطيء" (slow) class is the 10 profiles that carry SERVER TIMEOUT 2000.
    # Two rules keep the timeouts coherent:
    #  (a) the whole-request timeout must exceed the server window whenever it is set,
    #      otherwise the server window (and the ECMLOOPING round inside it) never runs;
    #  (b) the slow class is locked to its count, so a silent slide back to 800 fails.
    if glob.get("DEFAULT ENABLE ECMLOOPING", [""])[0].upper() != "YES":
        bad("DEFAULT ENABLE ECMLOOPING must be YES (R45: retries only when all servers NOK)")
    slow = 0
    for p in profs:
        kv = p["kv"]
        st = kv.get("SERVER TIMEOUT", [""])[0]
        dt = kv.get("DCW TIMEOUT", [""])[0]
        if st.isdigit() and st == "2000":
            slow += 1
        if st.isdigit() and dt.isdigit() and int(dt) <= int(st):
            bad("%s: DCW TIMEOUT %s must exceed SERVER TIMEOUT %s" % (p["name"], dt, st))
    if slow != 10:
        bad("slow class (SERVER TIMEOUT 2000) must be exactly 10 profiles, found %d" % slow)
    if len(fails) == fails_before_r45:
        ok("R45 timeouts: slow class=10, DCW TIMEOUT > SERVER TIMEOUT, ECMLOOPING on")

    for port, n in ports.items():
        if n > 1:
            bad("duplicate PORT %s in %d profiles" % (port, n))
    if len(ports) == len(profs):
        ok("72 profiles: unique ports, one family block each")

    # ---- summary ----
    for f in fails:
        print("  [FAIL] " + f)
    if fails:
        print("cfgcheck: %d failed, %d passed" % (len(fails), len(oks)))
        return 1
    for msg in oks:
        print("  [ ok ] " + msg)
    print("  [ ok ] dead keys: none (global or per-profile)")
    print("  [ ok ] family policy: VideoGuard CSA half+swap, iCam strict, fixed keys local")
    print("cfgcheck: %d checks ok" % (len(oks) + 2))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: cfgcheck.py <profiles.cfg>")
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
