#!/usr/bin/env python3
"""Force a Set-DCW scheduling stall in an isolated temporary build.

This characterizes/observes a narrow timing window, not a production timing
measurement or a test of normal scheduling. No source in the checkout changes.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='mcs-late-dispatch-') as folder:
    src = Path(folder) / 'src'
    shutil.copytree(root / 'src', src, ignore=shutil.ignore_patterns('x64', '__pycache__'))
    (src / 'x64').mkdir()
    path = src / 'setdcw.c'
    text = path.read_text()
    marker = '\t// Check timeout\n\tuint32_t ecmtime = GetTickCount()-ecm->recvtime;'
    assert text.count(marker) == 2
    pos = text.rindex(marker)
    text = text[:pos] + '\tusleep(1600000); /* isolated test: stall after success commit */\n' + text[pos:]
    path.write_text(text)
    build = subprocess.run(['make', '-C', str(src), '-B', 'target=x64', 'link'], capture_output=True, text=True, timeout=120)
    if build.returncode:
        print(build.stdout[-4000:] + build.stderr[-4000:], file=sys.stderr)
        raise SystemExit(build.returncode)
    env = dict(os.environ, MCS_TEST_RESPONSE='late')
    subprocess.run([sys.executable, str(root / 'tests/cccam-trace-success.py'), str(src / 'x64/multics')], env=env, check=True, timeout=60)
