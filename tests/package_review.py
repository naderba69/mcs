#!/usr/bin/env python3
"""package_review.py -- per-package risk review of a multics.cfg (R64).

For every profile it reports, from the file alone:
  * the CA system the code assigns to its CAID (exact row first, then the
    family row, exactly as src/casystem.h ca_table does);
  * whether the CW checksum gate is enforced for it (src/dcw.c
    acceptDCW_impl: enforced unless the profile says DCWFILTER CHECKSUM: OFF,
    or the profile is CA MODERN ON and the CAID carries no DVB-CSA checksum);
  * whether that CAID/provider is on the external "checksum disabled" list
    (OSCam disablecrccws_only_for, community configs, see docs/PACKAGE-PROFILES-R64-ar.md);
  * duplicate (CAID, provider) pairs across profiles;
  * ECM CHECK LENGTH state.

It is ADVISORY: it does not change the file and always exits 0. A flag means
"measure this before changing it", never "change it now".

Usage: python3 tests/package_review.py multics-r24-optimized.cfg
"""
import collections
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "protection_audit", os.path.join(HERE, "protection_audit.py"))
pa = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pa)

# Mirror of src/casystem.h ca_table (exact rows first, then family rows).
# (caid, exact, cwformat, name). cwformat: CSA / AES / CONST / UNKNOWN.
CA_TABLE = [
    (0x0963, 1, "AES", "VideoGuard iCam (Sky UK)"),
    (0x098C, 1, "AES", "VideoGuard (Sky DE, no-CRC CW)"),
    (0x098D, 1, "AES", "VideoGuard iCam (Sky DE)"),
    (0x098E, 1, "AES", "VideoGuard iCam"),
    (0x09C4, 1, "AES", "VideoGuard (Sky DE, no-CRC CW)"),
    (0x09CD, 1, "AES", "VideoGuard iCam (Sky IT)"),
    (0x2600, 1, "CONST", "BISS1"),
    (0x2610, 1, "AES", "BISS2"),
    (0x4AE0, 1, "CSA", "DRE-Crypt"),
    (0x4AE1, 1, "CSA", "DRE-Crypt"),
    (0x5601, 1, "AES", "Verimatrix"),
    (0x0100, 0, "CSA", "Seca Mediaguard"),
    (0x0500, 0, "CSA", "Viaccess"),
    (0x0600, 0, "CSA", "Irdeto"),
    (0x0900, 0, "CSA", "VideoGuard"),
    (0x0B00, 0, "CSA", "Conax"),
    (0x0D00, 0, "CSA", "CryptoWorks"),
    (0x0E00, 0, "CSA", "PowerVu"),
    (0x1700, 0, "CSA", "BetaCrypt"),
    (0x1800, 0, "CSA", "Nagravision"),
    (0x2600, 0, "CONST", "BISS"),
    (0x4A00, 0, "CSA", "DRE-Crypt"),
    (0x5600, 0, "AES", "Verimatrix"),
]

# CAID:provider pairs that the OSCam community configs exempt from the CW
# checksum (disablecrccws_only_for). Source: cccam.eu "oscam-conf-default-config"
# (2026) and the streamboard OSCam wiki; plus the SAT-4-ALL forum note on
# Conax 0B01 (2024). Provider 000000 is read as "every provider of that CAID"
# (our reading of OSCam's ident matching; not verified against OSCam source).
EXTERNAL_CRC_OFF = {
    ("0500", "030B00"), ("0500", "043800"), ("0500", "032830"), ("0500", "050F00"),
    ("098C", "000000"), ("098D", "000000"), ("09C4", "000000"),
    ("1708", "000000"), ("1709", "000000"),
    ("1811", "003311"),
    ("0E00", "000000"),
    ("1884", "000000"),
    ("092B", "000000"),
    ("0963", "000000"),
    ("0B01", "000000"),
}


def ca_info(caid):
    """Return (cwformat, name) for a CAID, as casystem.h would resolve it."""
    for c, exact, fmt, name in CA_TABLE:
        if exact and c == caid:
            return fmt, name
    for c, exact, fmt, name in CA_TABLE:
        if not exact and (c >> 8) == (caid >> 8):
            return fmt, name
    return "UNKNOWN", "Unknown"


def external_hit(caid_hex, providers):
    for prov in providers:
        if (caid_hex, prov) in EXTERNAL_CRC_OFF:
            return True
        if (caid_hex, "000000") in EXTERNAL_CRC_OFF and prov in ("000000", "*"):
            return True
    return False


def main(path):
    defaults, profiles = pa.parse(path)

    def get(p, key):
        # Same resolution order as protection_audit.effective():
        # profile line, DEFAULT, global rule, code default.
        return pa.effective(defaults, p, key)[0]

    rows = []
    pairs = collections.defaultdict(list)
    flags = collections.Counter()
    for p in profiles:
        caid_s = get(p, "CAID").split(",")[0].strip().upper()
        try:
            caid = int(caid_s, 16)
        except ValueError:
            continue
        caid_hex = "%04X" % caid
        fmt, ca_name = ca_info(caid)
        provs = [x.strip().upper() for x in get(p, "PROVIDERS").split(",") if x.strip()]
        if not provs:
            provs = ["000000"]
        ca_modern = get(p, "CA MODERN").upper() in ("YES", "ON", "1")
        ovr = get(p, "DCWFILTER CHECKSUM").upper()
        if ovr in ("OFF", "NO", "0"):
            enforced = False
            how = "off (profile override)"
        elif ovr in ("ON", "YES", "1"):
            enforced = True
            how = "on (profile override)"
        elif ca_modern and fmt in ("AES", "CONST"):
            enforced = False
            how = "off (CA MODERN, %s carries no checksum)" % fmt
        else:
            enforced = True
            how = "on (%s checksum CW)" % fmt
        ext = external_hit(caid_hex, provs)
        for pv in provs:
            pairs[(caid_hex, pv)].append(p["name"])
        ecm_len = get(p, "ECM CHECK LENGTH").upper() in ("YES", "ON", "1")
        risk = []
        if enforced and ext:
            risk.append("CRC-off-elsewhere")
            flags["checksum enforced where community turns it off"] += 1
        rows.append((p["name"], caid_hex, ca_name, fmt, ",".join(provs), how,
                     "YES" if ecm_len else "NO", ", ".join(risk) or "-"))

    print("# package review: %s" % os.path.basename(path))
    print("profiles: %d" % len(rows))
    print("")
    print("| profile | CAID | CA system (code) | CW format | providers | checksum gate | ECM LEN | flags |")
    print("|---|---|---|---|---|---|---|---|")
    for r in rows:
        print("| %s | %s | %s | %s | %s | %s | %s | %s |" % r)
    print("")
    dups = [(k, v) for k, v in sorted(pairs.items()) if len(v) > 1]
    print("duplicate (CAID, provider) pairs: %d" % len(dups))
    for (c, pv), names in dups:
        print("  %s:%s -> %s" % (c, pv, " | ".join(names)))
    print("")
    print("flag counts:")
    for k, v in sorted(flags.items()):
        print("  %s: %d" % (k, v))
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
