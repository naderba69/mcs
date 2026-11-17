#!/usr/bin/env python3
"""Band 72 live proof: a newcamd client's ECM - above all a rejection
before any ECM_DATA exists (cs_accept_ecm) - is visible on /user-trace
with its reason, and the client is selectable there at all.

Phase A: profile carries `ECM LENGTH: 42` (0x42=66) while the harness
ECM is 64 bytes -> server rejects "Wrong ecm length". Before band 72
this existed only in the debug log; the trace page counted the request
with no reason and the newcamd client was not even in its selector.
Phase B: same profile without the length list -> CW delivered, and the
trace must show the delivery event for the same client.

Rig: .ncserver.bin card (60 ms) + .ncclient.bin, ephemeral ports.
"""
import base64, re, socket, subprocess, sys, tempfile, time, urllib.parse, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def run_phase(extra_profile_lines, expect_client, expect_page):
    http, cache, pport, card = free_port(), free_port(), free_port(), free_port()
    with tempfile.TemporaryDirectory(prefix='mcs-nctrace-') as td:
        work = Path(td)
        cfg = work / 'one.cfg'
        cfg.write_text('\n'.join([
            f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin',
            'TELNET PORT: 0', f'CACHE PORT: {cache}',
            f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14',
            '', '[ trace1884 ]', f'PORT: {pport}', 'CAID: 1884', 'PROVIDERS: 0',
            *extra_profile_lines, 'USER: u1 p1']) + '\n')
        cardlog = open(work / 'card.log', 'w')
        cardp = subprocess.Popen([str(TESTS / '.ncserver.bin'), str(card), KEY, '1884', '000000'],
                                 stdout=cardlog, stderr=subprocess.STDOUT,
                                 env={'NS_DELAYS': '60', 'PATH': '/usr/bin:/bin'})
        srvlog = open(work / 'srv.log', 'w')
        srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(cfg)],
                                stdout=srvlog, stderr=subprocess.STDOUT, cwd=work)
        try:
            deadline = time.monotonic() + 15
            while 'advertised card' not in (work / 'card.log').read_text(errors='replace'):
                if time.monotonic() > deadline: raise AssertionError('card never connected')
                time.sleep(0.2)
            auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
            url = f'http://127.0.0.1:{http}/user-trace'
            def req(data=None):
                r = urllib.request.Request(url, data=data, headers={'Authorization': auth})
                return urllib.request.urlopen(r, timeout=5).read().decode('utf-8', errors='replace')
            page = req()
            opt = re.search(r"<option value='(\d+)'[^>]*>([^<]*u1[^<]*\[newcamd\][^<]*)</option>", page)
            assert opt, 'newcamd client u1 not selectable on /user-trace'
            tok = re.search(r"name='csrf' value='([a-f0-9]{64})'", page)
            assert tok, 'csrf token missing'
            req(urllib.parse.urlencode({'csrf': tok.group(1), 'action': 'start',
                                        'client': opt.group(1), 'mode': 'single',
                                        'caid': '1884', 'provid': '000000',
                                        'sid': '0064'}).encode())
            out = subprocess.run([str(TESTS / '.ncclient.bin'), '127.0.0.1', str(pport),
                                  'u1', 'p1', KEY, '0', '1884', '0064'],
                                 capture_output=True, text=True, timeout=30,
                                 env={'NC_ECMS': '1', 'PATH': '/usr/bin:/bin'})
            assert expect_client in out.stdout, f'client outcome missing {expect_client!r}:\n{out.stdout}'
            time.sleep(1)
            page = req()
            assert expect_page in page, f'trace page missing {expect_page!r}'
        finally:
            for p in (srvp, cardp):
                p.kill()
            for p in (srvp, cardp):
                p.wait(timeout=5)
            cardlog.close(); srvlog.close()


run_phase(['ECM LENGTH: 42'], 'decode-failed', 'Wrong ecm length')
print('newcamd-trace-live: phase A PASS (rejection reason on /user-trace)')
run_phase([], 'THE SERVER DELIVERED A CONTROL WORD', 'CW response sent; picture not verified')
print('newcamd-trace-live: phase B PASS (delivery event on /user-trace)')
print('newcamd-trace-live: PASS')
