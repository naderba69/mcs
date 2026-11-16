#!/usr/bin/env python3
"""Band 83 live proof: /smartfilter reads the ONE filter state.

Until band 83 the SuperSmartFilter state was `static` inside its
header, so config.o, httpserver.o and main.o each owned a private
copy: checks ran on main.o's tables while the console rendered
httpserver.o's counters -- permanently zero, however much the
filter actually checked. The state is now a single extern
instance (the pattern ssf_cfg always used), and this test proves
the console sees real checks: one delivered CW with the filter
armed must move Total Checked off zero.
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


def main():
    http, pport, card = free_port(), free_port(), free_port()
    with tempfile.TemporaryDirectory(prefix='mcs-ssfconsole-') as td:
        work = Path(td)
        (work / 'one.cfg').write_text('\n'.join([
            f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin', 'TELNET PORT: 0',
            f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14',
            '', '[ ssf1884 ]', f'PORT: {pport}', 'CAID: 1884', 'PROVIDERS: 0', 'FILTER: 1',
            'USER: u1 p1']) + '\n')
        cardp = subprocess.Popen([str(TESTS / '.ncserver.bin'), str(card), KEY, '1884', '000000'],
                                 stdout=open(work / 'card.log', 'w'), stderr=subprocess.STDOUT,
                                 env={'NS_DELAYS': '60', 'PATH': '/usr/bin:/bin'})
        srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'one.cfg')],
                                stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
        try:
            deadline = time.monotonic() + 20
            while 'advertised card' not in (work / 'card.log').read_text(errors='replace'):
                if time.monotonic() > deadline: raise AssertionError('card never connected')
                time.sleep(0.2)
            out = subprocess.run([str(TESTS / '.ncclient.bin'), '127.0.0.1', str(pport),
                                  'u1', 'p1', KEY, '0', '1884', '0064'],
                                 capture_output=True, text=True, timeout=40,
                                 env={'NC_ECMS': '2', 'PATH': '/usr/bin:/bin'})
            assert 'CONTROL WORD' in out.stdout, 'ECM not served:\n' + out.stdout
            auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
            r = urllib.request.Request(f'http://127.0.0.1:{http}/smartfilter',
                                       headers={'Authorization': auth})
            page = urllib.request.urlopen(r, timeout=5).read().decode('utf-8', errors='replace')
            m = re.search(r'Total Checked\D{0,40}?(\d+)|إجمالي المفحوص\D{0,40}?(\d+)', page)
            assert m, 'Total Checked row not found on /smartfilter'
            n = int(m.group(1) or m.group(2))
            assert n >= 1, f'console still reads a private zero copy (Total Checked={n})'
            print(f'ssfconsole-live: PASS (Total Checked={n} on /smartfilter)')
        finally:
            for p in (srvp, cardp): p.kill()
            for p in (srvp, cardp): p.wait(timeout=5)
    print('ssfconsole-live: PASS')


if __name__ == '__main__':
    main()
