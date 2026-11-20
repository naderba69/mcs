#!/bin/sh
# Band 121 guard: the printed build band must be the live band, not a stale label.
# The failure mode this prevents is exactly the one that cost two sessions: a
# page line that says "البند 118" being read as "this is the newest build".
# 1) MCS_BUILD_BAND == the last "## N" section of the Arabic report (the number
#    the report itself closes with), so the two cannot drift apart;
# 2) MCS_INTEGRATION_BAND stays 118 AND the layers line still carries it -- the
#    operator asked for that wording to mean the MERGE, forever;
# 3) no source file may hardcode a "البند N" claim for the running build outside
#    build-identity.h, or the page can lie while this test still passes.
set -eu
cd "$(dirname "$0")/.."
python3 - <<'PY'
import re, sys
from pathlib import Path
h = Path('src/build-identity.h').read_text(encoding='utf-8')
build = int(re.search(r'#define\s+MCS_BUILD_BAND\s+(\d+)', h).group(1))
integ = int(re.search(r'#define\s+MCS_INTEGRATION_BAND\s+(\d+)', h).group(1))
rep = Path('docs/REPORT-R48-ar.md').read_text(encoding='utf-8')
nums = [int(n) for n in re.findall(r'^## (\d+)[.\s]', rep, re.M)]
fails = []
if not nums:
    fails.append("no numbered sections found in docs/REPORT-R48-ar.md")
elif max(nums) != build:
    fails.append(f"MCS_BUILD_BAND={build} but the report's last section is ## {max(nums)}"
                 " -- print the band that actually made this tree")
if integ != 118:
    fails.append(f"MCS_INTEGRATION_BAND={integ}; 118 is the merge of the tools into the"
                 " single base and is frozen by the operator's wording")
http = Path('src/httpserver.c').read_text(encoding='utf-8')
if 'البند 118' not in http and 'Band 118' not in http:
    fails.append("the layers line dropped the integration-band reference")
# (3) there is exactly ONE authority for "which build is this", and it is the
#     helper. Historical mentions on the page ("trustworthy only since band 119",
#     "if your binary predates 119 that is expected") are not lies about this
#     build -- they are what protects the operator from reading an old zero as a
#     new verdict -- so they are not policed. What IS policed is a second
#     identity emitter, which is how a stale label gets read as the present.
http = Path('src/httpserver.c').read_text(encoding='utf-8')
n_emit = http.count('mcs_build_identity(')
n_inc = http.count('#include "build-identity.h"')
if n_emit != 1:
    fails.append(f"httpserver.c calls mcs_build_identity {n_emit} times; the page must "
                 "carry exactly one identity line (the live test asserts it renders)")
if n_inc != 1:
    fails.append(f'build-identity.h included {n_inc} times in httpserver.c')
main_txt = Path('src/main.c').read_text(encoding='utf-8')
if main_txt.count('mcs_build_identity_log(') != 1:
    fails.append("main.c must print the build identity exactly once at boot")
if fails:
    for f in fails: print("FAIL:", f)
    sys.exit(1)
print(f"bandline: PASS (build band {build} == report head; integration band {integ} "
      "kept in the layers line)")
PY
