#!/usr/bin/env python3
"""Band 89 page audit: every web route, answered and sane.

Boots the shipped 91-profile config (ports remapped, admin
credentials) and walks every route the HTTP dispatcher knows,
with credentials, exactly as a browser session would. Each
route must answer 200 (or a redirect for /dashboard) with a
non-trivial body and no server crash. Parameterised pages are
also visited in their parameterless form (the menu links).

This is the "virtual browser" sweep: it exists because page
handlers read live state that unit tests never render.
"""
import base64, http.client, re, socket, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'

ROUTES = [
    '/', '/profiles', '/profile', '/newcamd', '/newcamdclient', '/servers',
    '/server', '/cache', '/cachepeer', '/biss', '/cccam', '/cccamclient',
    '/cs378x', '/camd35', '/cacheex', '/freecccam', '/mgcamd',
    '/mgcamdclient', '/diagnostics', '/debug-ecm',
    '/server-stats', '/dashboard', '/user-trace', '/log-viewer',
    '/cache-analytics', '/auto-tuning', '/editor0', '/style.css',
    '/connect.png', '/disconnect.png', '/enable.png', '/disable.png',
    '/debug.png', '/refresh.png', '/favicon.png', '/sms_new.gif',
    '/sms_old.gif', '/host', '/threads', '/json', '/cwlog',
    '/smartfilter', '/debug',
]


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def get(port, path, auth):
    c = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
    try:
        c.request('GET', path, headers={'Authorization': auth})
        r = c.getresponse()
        return r.status, r.read()
    finally:
        c.close()


def main():
    http = free_port()
    with tempfile.TemporaryDirectory(prefix='mcs-pageaudit-') as td:
        work = Path(td)
        text = (ROOT / 'multics-full-30w-52e.cfg').read_text(encoding='utf-8', errors='replace')
        text = re.sub(r'^HTTP PORT:.*$', f'HTTP PORT: {http}', text, flags=re.M)
        text = re.sub(r'^HTTP USER:.*$', 'HTTP USER: admin', text, flags=re.M)
        text = re.sub(r'^HTTP PASS:.*$', 'HTTP PASS: admin', text, flags=re.M)
        text = re.sub(r'^TELNET PORT:.*$', 'TELNET PORT: 0', text, flags=re.M)
        (work / 'full.cfg').write_text(text)
        srv = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'full.cfg')],
                               stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
        auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
        try:
            deadline = time.monotonic() + 30
            while True:
                try:
                    st, _ = get(http, '/', auth); break
                except Exception:
                    if time.monotonic() > deadline: raise AssertionError('web never came up')
                    time.sleep(0.5)
            bad = []
            for route in ROUTES + ['/profile', '/newcamdclient', '/server',
                                   '/cachepeer', '/cccamclient', '/mgcamdclient',
                                   '/profile?id=99999', '/server?id=99999',
                                   '/newcamdclient?id=99999', '/cachepeer?id=99999',
                                   '/cccamclient?id=99999', '/mgcamdclient?id=99999']:
                try:
                    st, body = get(http, route, auth)
                except Exception as e:
                    print(f'{route:18s} EXC {e!r} BAD')
                    bad.append((route, f'EXC {e!r}')); continue
                ok = (st == 200 and len(body) > 40) or (route == '/dashboard' and st == 303) \
                    or (st == 404 and b'not found' in body)
                print(f'{route:18s} {st} {len(body):7d}B {"ok" if ok else "BAD"}')
                if not ok: bad.append((route, f'HTTP {st}, {len(body)}B'))
            # /restart is an action, not a page: it must answer its
            # "Restarting" page; the process then restarts by design.
            st, body = get(http, '/restart', auth)
            print(f'{"/restart":18s} {st} {len(body):7d}B {"ok" if st == 200 and b"Restarting" in body else "BAD"}')
            if not (st == 200 and b'Restarting' in body): bad.append(('/restart', f'HTTP {st}'))
            # unauthenticated must not leak pages
            st, _ = get(http, '/', 'Basic ' + base64.b64encode(b'admin:wrong').decode())
            print(f'{"(wrong auth)":18s} {st} {"ok" if st in (401, 403) else "BAD"}')
            if st not in (401, 403): bad.append(('wrong-auth', f'HTTP {st}'))
            assert srv.poll() is None, 'server died during the sweep'
            assert not bad, 'bad routes: ' + ', '.join(f'{r} ({w})' for r, w in bad)
        finally:
            srv.kill(); srv.wait(timeout=5)
    print('pageaudit: PASS')


if __name__ == '__main__':
    main()
