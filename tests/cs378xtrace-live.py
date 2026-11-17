#!/usr/bin/env python3
"""Band 74 live proof: cs378x and camd35 clients on /user-trace.

Band 73 wired their handlers and selector entries but had no test
client to prove it live. tests/cs378xclient.c (built from the server's
own msg-camd35.c) now drives both protocols:

  denied phase    only a 0500 profile exists, client asks 1884 ->
                  client gets 0x44, trace page shows
                  "Invalid CAID/PROVIDER" for the selected client
  delivered phase 1884 profile + card -> CW delivered, trace page
                  shows the delivery event

Run once per protocol (cs378x TCP, camd35 UDP).
"""
import base64, re, socket, subprocess, sys, tempfile, time, urllib.parse, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def run_phase(proto, profile_caid, with_card, expect_client, expect_page):
    http, cache, xport, pport, card = (free_port() for _ in range(5))
    label = 'cs378x' if proto == 'cs378x' else 'camd35'
    with tempfile.TemporaryDirectory(prefix=f'mcs-{label}trace-') as td:
        work = Path(td)
        lines = [f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin',
                 'TELNET PORT: 0', f'CACHE PORT: {cache}',
                 f'{proto.upper()} PORT: {xport}', f'{proto.upper()} USER: xu1 xp1', '']
        if with_card:
            lines.append(f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14')
        lines += ['', f'[ {label}trace ]', f'PORT: {pport}', f'CAID: {profile_caid}',
                  'PROVIDERS: 0', 'USER: u1 p1']
        cfg = work / 'one.cfg'; cfg.write_text('\n'.join(lines) + '\n')
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
            opt = re.search(r"<option value='(\d+)'[^>]*>([^<]*xu1[^<]*\[" + label + r"\][^<]*)</option>", page)
            assert opt, f'{label} client xu1 not selectable on /user-trace'
            tok = re.search(r"name='csrf' value='([a-f0-9]{64})'", page)
            assert tok, 'csrf token missing'
            req(urllib.parse.urlencode({'csrf': tok.group(1), 'action': 'start',
                                        'client': opt.group(1), 'mode': 'single',
                                        'caid': '1884', 'provid': '000000',
                                        'sid': '0064'}).encode())
            out = subprocess.run([str(TESTS / '.cs378xclient.bin'), proto, '127.0.0.1', str(xport),
                                  'xu1', 'xp1', '1884', '000000', '0064'],
                                 capture_output=True, text=True, timeout=30)
            assert expect_client in out.stdout, f'client outcome missing {expect_client!r}:\n{out.stdout}\n{out.stderr}'
            time.sleep(1)
            page = req()
            assert expect_page in page, f'trace page missing {expect_page!r}'
        finally:
            for p in procs: p.kill()
            for p in procs: p.wait(timeout=5)
            srvlog.close()


for proto in ('cs378x', 'camd35'):
    run_phase(proto, '0500', False, 'decode-failed', 'Invalid CAID/PROVIDER')
    print(f'cs378xtrace-live: {proto} phase A PASS (rejection reason on /user-trace)')
    run_phase(proto, '1884', True, 'THE SERVER DELIVERED A CONTROL WORD', 'CW response sent; picture not verified')
    print(f'cs378xtrace-live: {proto} phase B PASS (delivery event on /user-trace)')
print('cs378xtrace-live: PASS')
