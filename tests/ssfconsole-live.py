#!/usr/bin/env python3
"""Band 83/84 live proof: /smartfilter reads the ONE filter state,
armed through the documented global keys.

Until band 83 the SuperSmartFilter state was `static` inside its
header, so config.o, httpserver.o and main.o each owned a private
copy: checks ran on main.o's tables while the console rendered
httpserver.o's counters -- permanently zero, however much the
filter actually checked. The state is now a single extern
instance (the pattern ssf_cfg always used).

Band 84 arms the filter through the GLOBAL keys (no profile
FILTER override): `CACHE SF LEVEL: 1` in the documented
space-separated syntax. Two parser-era defects made that
impossible: the SF branch demanded a colon right after SF, and
supersmartfilter_init() ran after config parsing and wiped
ssf_cfg (with filter_level then forced to 0). Scenario 1 proves
the global keys now take effect; scenario 2 proves the filter
stays OFF when the operator writes nothing.
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


def scenario(work, global_lines):
    http, pport, card = free_port(), free_port(), free_port()
    (work / 'one.cfg').write_text('\n'.join([
        f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin', 'TELNET PORT: 0',
        *global_lines,
        f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14',
        '', '[ ssf1884 ]', f'PORT: {pport}', 'CAID: 1884', 'PROVIDERS: 0',
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
        return page, int(m.group(1) or m.group(2))
    finally:
        for p in (srvp, cardp): p.kill()
        for p in (srvp, cardp): p.wait(timeout=5)


def main():
    with tempfile.TemporaryDirectory(prefix='mcs-ssfconsole-') as td:
        page, n = scenario(Path(td), ['CACHE SF LEVEL: 1', 'CACHE SF TIME: 777'])
        assert n >= 1, f'console still reads a private zero copy (Total Checked={n})'
        assert 'Level 1' in page, 'global CACHE SF LEVEL: 1 did not reach the page snapshot'
        assert 'Window 777ms' in page, 'global CACHE SF TIME: 777 did not reach the page snapshot'
        print(f'ssfconsole-live: armed via global keys (Total Checked={n})')
        page, n = scenario(Path(td), [])
        assert n == 0 and 'Level 0' in page, \
            f'filter must stay OFF without operator keys (Total Checked={n})'
        print('ssfconsole-live: default OFF preserved without keys')
    print('ssfconsole-live: PASS')


if __name__ == '__main__':
    main()
