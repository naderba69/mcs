#!/usr/bin/env python3
"""Band 78 live proof: the cacheex protoban door (CACHEEX LOGIN FAILS/BAN).

PB_CACHEEX sat in the band-60 ban table with no door feeding it.
Cacheex peers enter through shared doors (CCcam user/pass, cs378x and
camd35 UCRC), so counting is attribution-based, documented in the
code: a failure counts under PB_CACHEEX only when the presented
identity IS a configured cacheex identity (here: a CCcam F: line with
cacheex_mode). Unknown identities stay with the door's own counter --
an unattributed guess cannot be told apart from a client guess.

Phase A (armed): 3 wrong-password logins as the cacheex user trip the
cacheex ban; the correct password is then refused at identification;
the ordinary CCcam user on the same IP is still served and no cccam
ban exists (per-identity separation). /user-trace shows the cacheex
ban. Phase B (unarmed, stock): the same wrong attempts change
nothing and the correct login succeeds.
"""
import base64, re, socket, subprocess, sys, tempfile, time
import urllib.request, urllib.error
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def login(ccbin, cport, user, pw):
    out = subprocess.run([str(ccbin), '127.0.0.1', str(cport), user, pw, '1884', '1136', '0x80'],
                         capture_output=True, text=True, timeout=25)
    return out.stdout


def page(http):
    auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
    r = urllib.request.Request(f'http://127.0.0.1:{http}/user-trace', headers={'Authorization': auth})
    with urllib.request.urlopen(r, timeout=5) as resp:
        return resp.read().decode('utf-8', errors='replace')


def boot(armed):
    http, cport, pport, card = (free_port() for _ in range(4))
    td = tempfile.TemporaryDirectory(prefix='mcs-cxban-')
    work = Path(td.name)
    lines = [f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin',
             'TELNET PORT: 0', f'CCCAM PORT: {cport}',
             'F: devuser devpass', 'F: cxuser cxpass { cacheex_mode=3 }',
             'CCCAM LOGIN FAILS: 3', 'CCCAM LOGIN BAN: 3600']
    if armed:
        lines += ['CACHEEX LOGIN FAILS: 3', 'CACHEEX LOGIN BAN: 120']
    lines += [f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14',
              '', '[ cxban ]', f'PORT: {pport}', 'CAID: 1884', 'PROVIDERS: 0',
              'USER: devuser devpass', '']
    (work / 'one.cfg').write_text('\n'.join(lines))
    cardp = subprocess.Popen([str(TESTS / '.ncserver.bin'), str(card), KEY, '1884', '000000'],
                             stdout=open(work / 'card.log', 'w'), stderr=subprocess.STDOUT,
                             env={'NS_DELAYS': '60', 'PATH': '/usr/bin:/bin'})
    srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'one.cfg')],
                            stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
    deadline = time.monotonic() + 20
    while 'advertised card' not in (work / 'card.log').read_text(errors='replace'):
        if time.monotonic() > deadline: raise AssertionError('card never connected')
        time.sleep(0.2)
    return td, (cardp, srvp), http, cport


def main():
    ccbin = TESTS / '.ccdcw.bin'
    subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(ccbin), str(TESTS / 'ccdcw.c')], check=True)
    if not (TESTS / '.ncserver.bin').exists():
        subprocess.run(['cc', '-O2', '-std=gnu89', '-w', '-o', str(TESTS / '.ncserver.bin'),
                        str(TESTS / 'ncserver.c'), str(ROOT / 'src/des.c'),
                        str(ROOT / 'src/msg-newcamd.c'), str(ROOT / 'src/md5.c')], check=True)

    # Phase A: armed
    td, procs, http, cport = boot(armed=True)
    try:
        assert 'DCW' in login(ccbin, cport, 'devuser', 'devpass'), 'baseline user session failed'
        time.sleep(1.5)
        for i in range(3):
            assert 'login as' not in login(ccbin, cport, 'cxuser', 'wrongpass'), f'cx wrong pass {i+1} accepted?!'
        assert 'login as' not in login(ccbin, cport, 'cxuser', 'cxpass'), \
            'cacheex ban not enforced: correct password accepted after threshold'
        p = page(http)
        assert re.search(r'cacheex — 127\.0\.0\.1', p), 'cacheex ban not shown on /user-trace'
        assert not re.search(r'cccam — 127\.0\.0\.1', p), 'cccam door wrongly banned (attribution leak)'
        time.sleep(1.5)
        assert 'DCW' in login(ccbin, cport, 'devuser', 'devpass'), \
            'ordinary user on same IP hurt by cacheex ban'
        print('cacheexban-live: phase A PASS (cacheex ban trips, enforced, per-identity)')
    finally:
        for p in procs: p.kill()
        for p in procs: p.wait(timeout=5)
        td.cleanup()

    # Phase B: unarmed = stock
    td, procs, http, cport = boot(armed=False)
    try:
        for i in range(3):
            assert 'login as' not in login(ccbin, cport, 'cxuser', 'wrongpass')
        assert 'login as' in login(ccbin, cport, 'cxuser', 'cxpass'), \
            'unarmed cacheex door must keep stock behavior (correct login succeeds)'
        print('cacheexban-live: phase B PASS (unarmed = stock)')
    finally:
        for p in procs: p.kill()
        for p in procs: p.wait(timeout=5)
        td.cleanup()
    print('cacheexban-live: PASS')


if __name__ == '__main__':
    main()
