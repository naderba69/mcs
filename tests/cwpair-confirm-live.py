#!/usr/bin/env python3
"""Band 82 live proof: CWPAIR confirmation and DoubleCheck monitoring
are complete on /user-trace (the operator never leaves the page).

Before band 82 the discovered-pairs list was bare copy-paste blocks:
no providers, no freshness, no idea whether a profile already
carries the pair -- confirming 1830<->1843 from the page alone was
guesswork. Now each pair row (most-confirmed first) shows both
providers, the match count, the age of the last match, and the
armed state (profile name + CWPAIR ACTIVE), and the DoubleCheck
summary carries the mismatch rate as the field reads it (n of total).

Rig: profiles 183D and 183E, each fed by its own newcamd card; both
cards answer SID 0064 with the same CW bytes (ncserver derives the
CW from the SID), so discovery learns the pair. The 183E profile
already carries CWPAIR (ACTIVE: NO): its row must say so.
"""
import base64, re, socket, subprocess, sys, tempfile, time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def ask(ncbin, port, caid):
    out = subprocess.run([str(ncbin), '127.0.0.1', str(port), 'u1', 'p1', KEY, '0', caid, '0064'],
                         capture_output=True, text=True, timeout=40,
                         env={'NC_ECMS': '1', 'PATH': '/usr/bin:/bin'})
    return out.stdout


def main():
    http, p183d, p183e, c183d, c183e = (free_port() for _ in range(5))
    with tempfile.TemporaryDirectory(prefix='mcs-cwconfirm-') as td:
        work = Path(td)
        prof = []   # stock DCW gates: the card CW passes as in the trace rigs
        (work / 'one.cfg').write_text('\n'.join([
            f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin', 'TELNET PORT: 0',
            'CWDEDUP CHECK: YES', 'CWPAIR DISCOVER CAID: 183D,183E',
            f'N: 127.0.0.1 {c183d} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14',
            f'N: 127.0.0.1 {c183e} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14',
            '', '[ twin183d ]', 'CAID: 183D', 'PROVIDERS: 000000', f'PORT: {p183d}',
            *prof, 'USER: u1 p1',
            '', '[ pair183e ]', 'CAID: 183E', 'PROVIDERS: 000000', f'PORT: {p183e}',
            *prof, 'CWPAIR CAID: 183D', 'CWPAIR ACTIVE: NO', 'CWPAIR SID: 0064->0064',
            'USER: u1 p1']) + '\n')
        procs = []
        for card, caid in ((c183d, '183D'), (c183e, '183E')):
            log = open(work / f'card{caid}.log', 'w')
            procs.append(subprocess.Popen(
                [str(TESTS / '.ncserver.bin'), str(card), KEY, caid, '000000'],
                stdout=log, stderr=subprocess.STDOUT,
                env={'NS_DELAYS': '60', 'PATH': '/usr/bin:/bin'}))
        srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'one.cfg')],
                                stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
        procs.append(srvp)
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                logs = (work / 'card183D.log').read_text(errors='replace') + \
                       (work / 'card183E.log').read_text(errors='replace')
                if logs.count('advertised card') >= 2: break
                time.sleep(0.2)
            else: raise AssertionError('cards never connected')
            out183d = ask(TESTS / '.ncclient.bin', p183d, '183D')
            assert 'CONTROL WORD' in out183d, '183D ECM not served:\n' + out183d + '\n---SRV---\n' + (work / 'srv.log').read_text(errors='replace')[-1500:]
            time.sleep(1)
            assert 'CONTROL WORD' in ask(TESTS / '.ncclient.bin', p183e, '183E'), '183E ECM not served'
            auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
            r = urllib.request.Request(f'http://127.0.0.1:{http}/user-trace',
                                       headers={'Authorization': auth})
            page = urllib.request.urlopen(r, timeout=5).read().decode('utf-8', errors='replace')
            i = page.find('أزواج مكتشفة')
            if i < 0:
                j = page.find('اقتران الشفرات')
                print('CWPAIR LINE:', page[j:j+300] if j>=0 else 'none')
                print('SRVLOG tail:', (work / 'srv.log').read_text(errors='replace')[-800:])
                raise AssertionError('discovered-pairs section missing (pair not learned?)')
            seg = page[i:i+3000]
            assert '183D:000000:0064' in seg and '183E:000000:0064' in seg, \
                'pair row lacks full channel identities (caid:provider:sid)'
            assert re.search(r'تطابقات: [1-9]', seg), 'pair row lacks a match count'
            assert 'آخر تطابق قبل' in seg, 'pair row lacks last-match age'
            assert 'مُهيّأ أصلًا' in seg and 'pair183e' in seg, \
                'armed state (profile already carrying the pair) not shown'
            assert 'CWPAIR ACTIVE: NO' in seg, 'armed profile ACTIVE state not shown'
            assert 'نسبة عدم التطابق:' in page, 'DoubleCheck mismatch rate missing'
            print('cwpair-confirm-live: PASS (pair confirmed from /user-trace: identity, matches, age, armed state; DC rate shown)')
        finally:
            for p in procs: p.kill()
            for p in procs: p.wait(timeout=5)
    print('cwpair-confirm-live: PASS')


if __name__ == '__main__':
    main()
