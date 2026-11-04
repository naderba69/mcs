#!/usr/bin/env python3
"""Test MINTIME source discrimination with an isolated modified worker.

The local/cache-origin cases substitute only the queued source type, keeping the
upstream server's numeric ID to model an ID collision. It does not exercise a
real Mgcamd sender or cache exchange. No checkout source is modified.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='mcs-mintime-') as folder:
    src = Path(folder) / 'src'
    shutil.copytree(root / 'src', src, ignore=shutil.ignore_patterns('x64', '__pycache__'))
    (src / 'x64').mkdir()
    path = src / 'setdcw.c'
    text = path.read_text()
    marker = '\t\t\t\tecm_setdcwdata( ecm, dcw, srctype, srcid, cwmark, trace_at_queue, lifetime_at_queue );'
    assert text.count(marker) == 1
    injected = """                if (getenv("MCS_TEST_MINTIME_ORIGIN")) {
                    if (!strcmp(getenv("MCS_TEST_MINTIME_ORIGIN"), "local")) srctype = DCW_SOURCE_MGCLIENT;
                    else if (!strcmp(getenv("MCS_TEST_MINTIME_ORIGIN"), "cache")) srctype = DCW_SOURCE_CACHE;
                }
"""
    path.write_text(text.replace(marker, injected + marker))
    build = subprocess.run(['make', '-C', str(src), '-B', 'target=x64', 'link'], capture_output=True, text=True, timeout=120)
    if build.returncode:
        print(build.stdout[-4000:] + build.stderr[-4000:], file=sys.stderr)
        raise SystemExit(build.returncode)
    for origin in ('local', 'cache'):
        env = dict(os.environ, MCS_TEST_RESPONSE='mintime-' + origin, MCS_TEST_MINTIME_ORIGIN=origin)
        subprocess.run([sys.executable, str(root / 'tests/cccam-trace-success.py'), str(src / 'x64/multics')], env=env, check=True, timeout=60)
        print('PASS: ' + origin + ' source not rejected by server MINTIME when IDs collide', flush=True)
