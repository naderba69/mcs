#!/usr/bin/env python3
"""Verify the expert template remains a safe, synchronized baseline."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tests'))
from cfgcheck import parse

subprocess.run([sys.executable, str(ROOT/'tools/generate-expert-config.py'), '--check'], check=True)
base, old = parse(ROOT/'multics-r24-optimized.cfg')
template, new = parse(ROOT/'multics-r24-expert-reviewed.cfg')
assert len(old) == len(new) == 72
assert set(template) == set(base)
for key in base:
    if key not in {'HTTP PORT', 'HTTP USER', 'HTTP PASS'}:
        assert template[key] == base[key], key
assert template['HTTP PORT'] == ['0']
for before, after in zip(old, new):
    assert before['name'] == after['name']
    assert set(before['kv']) == set(after['kv'])
    for key, values in before['kv'].items():
        assert [v.split('#', 1)[0].strip() for v in values] == after['kv'][key], (before['name'], key)
print('PASS: 72 profiles preserve all directives; HTTP management is disabled')
