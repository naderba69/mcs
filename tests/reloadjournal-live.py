#!/usr/bin/env python3
"""Band 95 proof: the reload journal on /user-trace names, from real
events, who a browser save dropped and why -- and stays silent when
a save dropped nobody.

Operator topology (same modem for browser + receiver, other users
elsewhere) made every previous report ambiguous: global churn
theories could not be separated from per-user drops without data.
The journal removes the guesswork. Scenario:
  1. live Newcamd session + whole-file save, nothing changed:
     session completes; journal shows reload-start/reload-end and
     NO drop for the user.
  2. second live session + save that changes that user's password:
     stock behavior drops the session, and the journal must record
     drop-client/newcamd/<user> with the user-flag reason.
"""
import base64, http.client, os, socket, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'
AUTH = {'Authorization': 'Basic ' + base64.b64encode(b'admin:admin').decode()}


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def save(port, body):
    b = '----WebKitFormBoundary7MA4YWxkTrZu0gW'
    payload = (f'--{b}\r\nContent-Disposition: form-data; name="textedit"\r\n\r\n').encode() \
        + body + f'\r\n--{b}--\r\n'.encode()
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=20)
    conn.request('POST', '/editor0', body=payload, headers={
        'Content-Type': f'multipart/form-data; boundary={b}', **AUTH})
    resp = conn.getresponse(); page = resp.read().decode(errors='replace')
    assert resp.status == 200 and 'Successfully Saved' in page, f'save failed: HTTP {resp.status}'


def trace(port):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=20)
    conn.request('GET', '/user-trace', headers=AUTH)
    resp = conn.getresponse(); page = resp.read().decode('utf-8', 'replace')
    assert resp.status == 200, f'/user-trace HTTP {resp.status}'
    return page


def main():
    with tempfile.TemporaryDirectory(prefix='mcs-rjournal-') as td:
        tmp = Path(td)
        http_port, cccam, newcamd, card = free_port(), free_port(), free_port(), free_port()
        cfg = tmp / 'one.cfg'
        template = (
            f'HTTP PORT: {http_port}\nHTTP USER: admin\nHTTP PASS: admin\nTELNET PORT: 0\n'
            f'CCCAM PORT: {cccam}\nF: traceuser tracepass\n'
            f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n'
            f'\n[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n'
            f'USER: traceuser {{PASS}}\n')
        cfg.write_text(template.replace('{PASS}', 'tracepass'))
        peer = subprocess.Popen([str(TESTS / '.ncserver.bin'), str(card), KEY, '1884', '000000'],
                                cwd=tmp, env=dict(os.environ, NS_DELAYS='400'),
                                stdout=open(tmp / 'peer.log', 'w'), stderr=subprocess.STDOUT)
        srv = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(cfg)],
                               cwd=tmp, stdout=open(tmp / 'srv.log', 'w'), stderr=subprocess.STDOUT)
        sessions = []
        try:
            deadline = time.monotonic() + 20
            while 'advertised card' not in (tmp / 'peer.log').read_text(errors='replace'):
                if time.monotonic() > deadline: raise AssertionError('card never connected')
                time.sleep(0.2)

            def session():
                p = subprocess.Popen([str(TESTS / '.ncclient.bin'), '127.0.0.1', str(newcamd),
                                      'traceuser', 'tracepass', KEY, '0', '1884', '0064'],
                                     env=dict(os.environ, NC_ECMS='30', PATH='/usr/bin:/bin'),
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                sessions.append(p); return p

            # 1) unchanged save under a live session
            s1 = session(); time.sleep(3)
            save(http_port, cfg.read_bytes() + b'\n# no-op save\n')
            out1, _ = s1.communicate(timeout=180)
            assert 'CONTROL WORD' in out1 and s1.returncode == 0, \
                f'unchanged save broke the session\n{out1[-300:]}'
            page = trace(http_port)
            assert 'reload-start' in page and 'reload-end' in page, 'journal missing reload boundaries'
            assert 'drop-client' not in page, 'unchanged save journaled a client drop that never happened'
            print('reloadjournal: unchanged save -- session fine, journal clean')

            # 2) password-changing save under a live session
            s2 = session(); time.sleep(3)
            changed = template.replace('{PASS}', 'otherpass').encode()
            save(http_port, changed)
            try:
                s2.communicate(timeout=60)
            except subprocess.TimeoutExpired:
                s2.kill(); s2.communicate()
            deadline = time.monotonic() + 20
            page = ''
            while time.monotonic() < deadline:
                page = trace(http_port)
                if 'drop-client' in page: break
                time.sleep(0.5)
            assert 'drop-client' in page and 'traceuser' in page, \
                'password-change drop not journaled with the user name'
            print('reloadjournal: password-change save -- drop journaled (newcamd/traceuser)')
        finally:
            for p in sessions + [srv, peer]:
                if p.poll() is None: p.kill()
            for p in sessions + [srv, peer]:
                try: p.wait(timeout=5)
                except Exception: pass
    print('reloadjournal-live: PASS')


if __name__ == '__main__':
    main()
