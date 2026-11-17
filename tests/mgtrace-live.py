#!/usr/bin/env python3
"""Band 73 live proof (mgcamd): an mgcamd client's ECM - including a
rejection before any ECM_DATA exists - is visible on /user-trace with
its reason, and the client is selectable there.

Phase A: client asks for CAID 1884 but the only profile is 0500 ->
"Invalid CAID/PROVIDER" must appear on the trace page.
Phase B: profile 1884 with a real card -> CW delivered, delivery event
on the page ("CW response sent; picture not verified").

mgcamd shares newcamd's cs_message framing, so .ncclient.bin is the
client (same trick as tests/mgban.sh). cs378x/camd35 have no test
client in the tree; their handlers mirror this code path-for-path and
their clients are covered here only for selector visibility when a
full config defines them.
"""
import base64, os, re, socket, subprocess, sys, tempfile, time, urllib.parse, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def run_phase(profile_caid, with_card, expect_client, expect_page):
    http, cache, mport, pport, card = (free_port() for _ in range(5))
    with tempfile.TemporaryDirectory(prefix='mcs-mgtrace-') as td:
        work = Path(td)
        cfg = work / 'one.cfg'
        lines = [f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin',
                 'TELNET PORT: 0', f'CACHE PORT: {cache}',
                 f'MGCAMD PORT: {mport}', 'MGCAMD USER: mu1 mp1', '']
        if with_card:
            lines.append(f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14')
        lines += ['', '[ mgtrace ]', f'PORT: {pport}', f'CAID: {profile_caid}',
                  'PROVIDERS: 0', 'USER: u1 p1']
        cfg.write_text('\n'.join(lines) + '\n')
        procs = []
        if with_card:
            cardlog = open(work / 'card.log', 'w')
            procs.append(subprocess.Popen([str(TESTS / '.ncserver.bin'), str(card), KEY, '1884', '000000'],
                                          stdout=cardlog, stderr=subprocess.STDOUT,
                                          env={'NS_DELAYS': '60', 'PATH': '/usr/bin:/bin'}))
        srvlog = open(work / 'srv.log', 'w')
        procs.append(subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(cfg)],
                                      stdout=srvlog, stderr=subprocess.STDOUT, cwd=work))
        try:
            if with_card:
                deadline = time.monotonic() + 15
                while 'advertised card' not in (work / 'card.log').read_text(errors='replace'):
                    if time.monotonic() > deadline: raise AssertionError('card never connected')
                    time.sleep(0.2)
            auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
            url = f'http://127.0.0.1:{http}/user-trace'
            def req(data=None):
                r = urllib.request.Request(url, data=data, headers={'Authorization': auth})
                return urllib.request.urlopen(r, timeout=5).read().decode('utf-8', errors='replace')
            deadline = time.monotonic() + 15
            while True:
                try: page = req(); break
                except OSError:
                    if time.monotonic() > deadline: raise
                    time.sleep(0.2)
            opt = re.search(r"<option value='(\d+)'[^>]*>([^<]*mu1[^<]*\[mgcamd\][^<]*)</option>", page)
            assert opt, 'mgcamd client mu1 not selectable on /user-trace'
            tok = re.search(r"name='csrf' value='([a-f0-9]{64})'", page)
            assert tok, 'csrf token missing'
            req(urllib.parse.urlencode({'csrf': tok.group(1), 'action': 'start',
                                        'client': opt.group(1), 'mode': 'single',
                                        'caid': '1884', 'provid': '000000',
                                        'sid': '0064'}).encode())
            out = subprocess.run([str(TESTS / '.ncclient.bin'), '127.0.0.1', str(mport),
                                  'mu1', 'mp1', KEY, '0', '1884', '0064'],
                                 capture_output=True, text=True, timeout=30,
                                 env={'NC_ECMS': '1', 'PATH': '/usr/bin:/bin'})
            assert expect_client in out.stdout, f'client outcome missing {expect_client!r}:\n{out.stdout}'
            time.sleep(1)
            page = req()
            assert expect_page in page, f'trace page missing {expect_page!r}'
        finally:
            for p in procs: p.kill()
            for p in procs: p.wait(timeout=5)
            srvlog.close()


run_phase('0500', False, 'decode-failed', 'Invalid CAID/PROVIDER')
print('mgtrace-live: phase A PASS (mgcamd rejection reason on /user-trace)')
run_phase('1884', True, 'THE SERVER DELIVERED A CONTROL WORD', 'CW response sent; picture not verified')
print('mgtrace-live: phase B PASS (mgcamd delivery event on /user-trace)')
print('mgtrace-live: PASS')
