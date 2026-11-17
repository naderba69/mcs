#!/usr/bin/env python3
"""Band 86 audit: the shipped 30W-52E production config, booted
for real, must show its declared settings in effect.

cfgcheck (static) proves no key is unknown; this audit proves the
keys are not silently dead: it boots a port-remapped copy exactly
like prof-full, then reads /smartfilter and asserts the global
snapshot line and every declared per-profile FILTER level match
the file. This is the class of failure band 84 fixed for CACHE SF
(keys parsed, then wiped or gated away) -- a static allowlist
cannot see it, only the running binary can.
"""
import base64, re, socket, subprocess, sys, tempfile, time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
CFG = ROOT / 'multics-full-30w-52e.cfg'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def declared():
    """(global snapshot fragments, {profile_name: FILTER level}) from the file."""
    text = CFG.read_text(encoding='utf-8', errors='replace')
    g = dict(re.findall(r'^CACHE SF (LEVEL|TIME|BADCWRATIO|BADCWRATIOMIN|MINREPLIES|UNBLOCK):\s*(\d+)',
                        text, re.M))
    profiles = {}
    current = None
    for line in text.splitlines():
        m = re.match(r'^\[\s*(.+?)\s*\]', line)
        if m: current = m.group(1); continue
        m = re.match(r'^FILTER:\s*(\d+)', line)
        if m and current: profiles[current] = int(m.group(1))
    return g, profiles


def main():
    g, profiles = declared()
    assert g.get('LEVEL'), 'no global CACHE SF LEVEL declared in the shipped file'
    with tempfile.TemporaryDirectory(prefix='mcs-readycfg-') as td:
        work = Path(td)
        http = free_port()
        text = CFG.read_text(encoding='utf-8', errors='replace')
        text = re.sub(r'^HTTP PORT:.*$', f'HTTP PORT: {http}', text, flags=re.M)
        text = re.sub(r'^HTTP USER:.*$', 'HTTP USER: a', text, flags=re.M)
        text = re.sub(r'^HTTP PASS:.*$', 'HTTP PASS: a', text, flags=re.M)
        text = re.sub(r'^TELNET PORT:.*$', 'TELNET PORT: 0', text, flags=re.M)
        (work / 'full.cfg').write_text(text)
        srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'full.cfg')],
                                stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
        try:
            auth = 'Basic ' + base64.b64encode(b'a:a').decode()
            page = None
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                try:
                    r = urllib.request.Request(f'http://127.0.0.1:{http}/smartfilter',
                                               headers={'Authorization': auth})
                    page = urllib.request.urlopen(r, timeout=5).read().decode('utf-8', errors='replace')
                    break
                except Exception:
                    time.sleep(0.5)
            assert page, 'server never answered /smartfilter'
            flat = re.sub(r'<[^>]+>', '|', page)
            parts = [p.strip() for p in flat.split('|') if p.strip()]
            joined = ' · '.join(parts)
            snap = next((p for p in parts if p.startswith('Level ')), '')
            assert snap.startswith(f"Level {g['LEVEL']} "), f'global LEVEL not in effect: {snap!r}'
            assert f"Window {g['TIME']}ms" in snap, f'global TIME not in effect: {snap!r}'
            assert f"BadCW ratio {g['BADCWRATIO']}%/{g['BADCWRATIOMIN']}" in snap, \
                f'global BADCWRATIO not in effect: {snap!r}'
            assert f"Min replies {g['MINREPLIES']}" in snap, f'MINREPLIES not in effect: {snap!r}'
            assert f"Unblock {g['UNBLOCK']}s" in snap, f'UNBLOCK not in effect: {snap!r}'
            checked = 0
            for name, lvl in profiles.items():
                i = next((k for k, p in enumerate(parts) if p == name), None)
                assert i is not None, f'profile {name!r} missing from /smartfilter table'
                row = parts[i:i + 4]
                assert str(lvl) in row[1:], f'profile {name!r} FILTER {lvl} not in effect: {row!r}'
                checked += 1
        finally:
            srvp.kill(); srvp.wait(timeout=5)
    print(f'readycfg-audit: PASS (global Level {g["LEVEL"]}, {checked} profile FILTER levels in effect)')
    print('readycfg-audit: PASS')


if __name__ == '__main__':
    main()
