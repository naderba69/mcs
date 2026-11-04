#!/usr/bin/env python3
"""Check that a queued CW without the matching trace lifetime is not attributed.

The temporary worker changes only the queued diagnostic token; delivery and
CW validation remain unchanged. This is not a test of ECM recycling itself.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='mcs-setdcw-identity-') as folder:
    src = Path(folder) / 'src'
    shutil.copytree(root / 'src', src, ignore=shutil.ignore_patterns('x64', '__pycache__'))
    (src / 'x64').mkdir()
    path = src / 'setdcw.c'
    text = path.read_text()
    marker = '\t\t\t\tget_setdcwdata(buf, &ecm, dcw, &srctype, &srcid, &cwmark, &trace_at_queue, &lifetime_at_queue);'
    assert text.count(marker) == 1
    injection = '''
                if (getenv("MCS_TEST_QUEUE_TRACE")) {
                    if (!strcmp(getenv("MCS_TEST_QUEUE_TRACE"), "zero")) trace_at_queue = 0;
                    else if (!strcmp(getenv("MCS_TEST_QUEUE_TRACE"), "different")) trace_at_queue ^= 0x8000000000000000ULL;
                }'''
    path.write_text(text.replace(marker, marker + injection))
    build = subprocess.run(['make', '-C', str(src), '-B', 'target=x64', 'link'], capture_output=True, text=True, timeout=120)
    if build.returncode:
        print(build.stdout[-4000:] + build.stderr[-4000:], file=sys.stderr)
        raise SystemExit(build.returncode)
    for token in ('zero', 'different'):
        env = dict(os.environ, MCS_TEST_RESPONSE='non09half', MCS_TEST_QUEUE_TRACE=token)
        subprocess.run([sys.executable, str(root / 'tests/cccam-trace-success.py'), str(src / 'x64/multics')], env=env, check=True, timeout=60)
        print('PASS: queued trace lifetime ' + token, flush=True)
        env = dict(os.environ, MCS_TEST_QUEUE_TRACE=token, MCS_EXPECT_NO_REJECTION_TRACE='1')
        subprocess.run([sys.executable, str(root / 'tests/cccam-dcw-check-trace.py'), str(src / 'x64/multics')], env=env, check=True, timeout=60)
        print('PASS: DCW CHECK rejection not bound to queued trace lifetime ' + token, flush=True)
