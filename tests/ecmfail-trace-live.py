#!/usr/bin/env python3
"""Band 80 live proof: shared-engine ECM failures carry their real reason.

Before band 80 the shared ECM engine's terminal failure traced one
generic line ("ECM failed; reason may be unavailable") -- and three
call sites emitted their specific FAILED first, so the vague line
appeared as a duplicate after the real one. Every client door then
reported "decode failed; detailed reason unavailable". Now
ecm_faileddcw takes the caller's reason, emits FAILED once with it,
and the delivery snapshot carries statusmsg so each client's result
reads "decode failed (<reason>)".

Rig: a profile with NO server lines at all. A newcamd ECM can only
fail in the shared engine (dispatch window expires / DCW timeout).
The trace must show the detailed reason and neither generic string.
"""
import base64, re, socket, subprocess, sys, tempfile, time
import urllib.parse, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def main():
    http, pport = free_port(), free_port()
    with tempfile.TemporaryDirectory(prefix='mcs-ecmfail-') as td:
        work = Path(td)
        (work / 'one.cfg').write_text('\n'.join([
            f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin',
            'TELNET PORT: 0', '',
            '[ fail1884 ]', f'PORT: {pport}', 'CAID: 1884', 'PROVIDERS: 0',
            'USER: u1 p1']) + '\n')
        srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'one.cfg')],
                                stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
        try:
            auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
            url = f'http://127.0.0.1:{http}/user-trace'
            def req(data=None):
                r = urllib.request.Request(url, data=data, headers={'Authorization': auth})
                return urllib.request.urlopen(r, timeout=5).read().decode('utf-8', errors='replace')
            deadline = time.monotonic() + 15
            while True:
                try:
                    page = req(); break
                except Exception:
                    if time.monotonic() > deadline: raise AssertionError('server never came up')
                    time.sleep(0.3)
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
                                 capture_output=True, text=True, timeout=60,
                                 env={'NC_ECMS': '1', 'PATH': '/usr/bin:/bin'})
            assert 'decode-failed' in out.stdout, f'client did not see the failure:\n{out.stdout}'
            time.sleep(1)
            page = req()
            assert 'ECM failed; reason may be unavailable' not in page, 'generic shared reason still traced'
            assert 'detailed reason unavailable' not in page, 'generic client reason still traced'
            detailed = [r for r in ('DCW timeout', 'no pending server', 'no server open',
                                    'maximum servers reached', 'no free server')
                        if r in page]
            assert detailed, 'no detailed failure reason on /user-trace'
            assert 'decode failed (' in page, 'client result does not carry the reason'
            print(f'ecmfail-trace-live: PASS (reason: {detailed[0]}; client result detailed)')
        finally:
            srvp.kill(); srvp.wait(timeout=5)
    print('ecmfail-trace-live: PASS')


if __name__ == '__main__':
    main()
