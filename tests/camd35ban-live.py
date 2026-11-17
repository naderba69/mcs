#!/usr/bin/env python3
"""Band 76 live proof: camd35's protoban door (CAMD35 LOGIN FAILS/BAN).

PB_CAMD35 existed in the ban table since band 60 but no door fed it:
camd35 is UDP and identifies clients by UCRC alone, so credential
guessing was uncounted forever. Now:
  - 3 datagrams with an unknown UCRC (wrong user) trip the ban,
  - a subsequent datagram from the SAME IP with the CORRECT user is
    dropped too (ban enforced, no reply),
  - /user-trace lists the active camd35 ban and the audit ring shows
    the ucrc failures.
With CAMD35 LOGIN FAILS unset the same bad datagrams change nothing
(good client still served) - proven in phase B by a fresh boot
without the keys.
"""
import base64, re, socket, subprocess, sys, tempfile, time, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def boot(armed):
    http, cache, xport, pport, card = (free_port() for _ in range(5))
    td = tempfile.TemporaryDirectory(prefix='mcs-c35ban-')
    work = Path(td.name)
    lines = [f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin',
             'TELNET PORT: 0', f'CACHE PORT: {cache}',
             f'CAMD35 PORT: {xport}', 'CAMD35 USER: xu1 xp1']
    if armed:
        lines += ['CAMD35 LOGIN FAILS: 3', 'CAMD35 LOGIN BAN: 120']
    lines += ['', f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14',
              '', '[ c35ban ]', f'PORT: {pport}', 'CAID: 1884', 'PROVIDERS: 0', 'USER: u1 p1']
    (work / 'one.cfg').write_text('\n'.join(lines) + '\n')
    cardlog = open(work / 'card.log', 'w')
    cardp = subprocess.Popen([str(TESTS / '.ncserver.bin'), str(card), KEY, '1884', '000000'],
                             stdout=cardlog, stderr=subprocess.STDOUT,
                             env={'NS_DELAYS': '60', 'PATH': '/usr/bin:/bin'})
    srvlog = open(work / 'srv.log', 'w')
    srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'one.cfg')],
                            stdout=srvlog, stderr=subprocess.STDOUT, cwd=work)
    deadline = time.monotonic() + 15
    while 'advertised card' not in (work / 'card.log').read_text(errors='replace'):
        if time.monotonic() > deadline: raise AssertionError('card never connected')
        time.sleep(0.2)
    return td, work, (cardp, srvp), http, xport


def client(xport, user, pw):
    out = subprocess.run([str(TESTS / '.cs378xclient.bin'), 'camd35', '127.0.0.1', str(xport),
                          user, pw, '1884', '000000', '0064'],
                         capture_output=True, text=True, timeout=30)
    return out.stdout


def page(http):
    auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
    r = urllib.request.Request(f'http://127.0.0.1:{http}/user-trace', headers={'Authorization': auth})
    return urllib.request.urlopen(r, timeout=5).read().decode('utf-8', errors='replace')


# Phase A: armed -> ban trips, good user from same IP dropped too
td, work, procs, http, xport = boot(armed=True)
try:
    for i in range(3):
        assert 'decode-failed' in client(xport, 'baduser', 'badpass'), f'bad attempt {i+1} not rejected'
    assert 'decode-failed' in client(xport, 'xu1', 'xp1'), 'banned IP must drop even the correct user'
    p = page(http)
    assert 'camd35' in p and 'نشط الآن: 1' in p, 'active camd35 ban not shown on /user-trace'
    assert re.search(r'ucrc:[0-9a-f]{8}', p), 'camd35 failure not in login audit ring'
    print('camd35ban-live: phase A PASS (ban trips at 3, enforced, visible on /user-trace)')
finally:
    for p in procs: p.kill()
    for p in procs: p.wait(timeout=5)
    td.cleanup()

# Phase B: unarmed (stock) -> bad datagrams counted nowhere, good user served
td, work, procs, http, xport = boot(armed=False)
try:
    for i in range(3):
        assert 'decode-failed' in client(xport, 'baduser', 'badpass')
    assert 'THE SERVER DELIVERED A CONTROL WORD' in client(xport, 'xu1', 'xp1'), \
        'unarmed door must keep serving the good user (stock behavior)'
    print('camd35ban-live: phase B PASS (unarmed = stock, good user served)')
finally:
    for p in procs: p.kill()
    for p in procs: p.wait(timeout=5)
    td.cleanup()
print('camd35ban-live: PASS')
