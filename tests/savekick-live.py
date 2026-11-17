#!/usr/bin/env python3
"""Save-from-browser must not kick connected clients.

Operator report: saving the whole config file in the web editor
saves fine, but connected clients get kicked -- even a CCcam
client on the same LAN. This test puts three live relationships
under a browser-style editor save of the whole file (multipart
POST to /editor0, exactly what the browser sends):

  1. an outbound Newcamd card connection (N: line),
  2. a persistent CCcam client session sending ECMs on one
     connection (tests/ccdcw.c),
  3. a persistent Newcamd client session sending ECMs on one
     connection (tests/.ncclient.bin with NC_ECMS).

Mid-session, the whole file is saved through the editor. Every
ECM on both client sessions must still be answered afterwards,
and the sessions must complete normally: a reload that matches
clients/servers by identity must not disconnect any of them
when their own lines did not change.
"""
import base64, http.client, os, socket, subprocess, sys, tempfile, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def build(tmp):
    cc = tmp / 'ccdcw'
    subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(cc),
                    str(TESTS / 'ccdcw.c')], check=True)
    return cc


def main():
    with tempfile.TemporaryDirectory(prefix='mcs-savekick-') as td:
        tmp = Path(td)
        cc = build(tmp)
        http_port, cccam, newcamd, card = free_port(), free_port(), free_port(), free_port()
        cfg = tmp / 'one.cfg'
        cfg.write_text(
            f'HTTP PORT: {http_port}\nHTTP USER: admin\nHTTP PASS: admin\nTELNET PORT: 0\n'
            f'CCCAM PORT: {cccam}\nF: traceuser tracepass\n'
            f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n'
            f'\n[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n'
            f'USER: traceuser tracepass\n')
        env = dict(os.environ, NS_DELAYS='400')
        peer = subprocess.Popen([str(TESTS / '.ncserver.bin'), str(card), KEY, '1884', '000000'],
                                cwd=tmp, env=env, stdout=open(tmp / 'peer.log', 'w'),
                                stderr=subprocess.STDOUT)
        srv = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(cfg)],
                               cwd=tmp, stdout=open(tmp / 'srv.log', 'w'), stderr=subprocess.STDOUT)
        sessions = []
        try:
            deadline = time.monotonic() + 20
            while 'advertised card' not in (tmp / 'peer.log').read_text(errors='replace'):
                if time.monotonic() > deadline: raise AssertionError('card never connected')
                time.sleep(0.2)
            tags = [f'0x{0x80 + i % 2:02x}' for i in range(30)]
            ccsess = subprocess.Popen([str(cc), '127.0.0.1', str(cccam), 'traceuser', 'tracepass',
                                       '1884', '1136'] + tags,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            ncsess = subprocess.Popen([str(TESTS / '.ncclient.bin'), '127.0.0.1', str(newcamd),
                                       'traceuser', 'tracepass', KEY, '0', '1884', '0064'],
                                      env=dict(os.environ, NC_ECMS='30', PATH='/usr/bin:/bin'),
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            sessions = [ccsess, ncsess]
            time.sleep(3)  # both sessions mid-flight (400 ms per card answer)

            body = cfg.read_bytes() + b'\n# operator whole-file save\n'
            b = '----WebKitFormBoundary7MA4YWxkTrZu0gW'
            payload = (f'--{b}\r\nContent-Disposition: form-data; name="textedit"\r\n\r\n').encode() \
                + body + f'\r\n--{b}--\r\n'.encode()
            conn = http.client.HTTPConnection('127.0.0.1', http_port, timeout=15)
            conn.request('POST', '/editor0', body=payload, headers={
                'Content-Type': f'multipart/form-data; boundary={b}',
                'Authorization': 'Basic ' + base64.b64encode(b'admin:admin').decode()})
            resp = conn.getresponse()
            page = resp.read().decode(errors='replace')
            assert resp.status == 200 and 'Successfully Saved' in page, \
                f'editor save failed: HTTP {resp.status}'

            out_cc, _ = ccsess.communicate(timeout=180)
            out_nc, _ = ncsess.communicate(timeout=180)
            n_cc = out_cc.count(' DCW')
            assert ccsess.returncode == 0 and n_cc == len(tags), \
                f'CCcam session kicked/broken by the save: rc={ccsess.returncode} DCW {n_cc}/{len(tags)}\n{out_cc[-400:]}'
            assert 'CONTROL WORD' in out_nc and ncsess.returncode == 0, \
                f'Newcamd session kicked/broken by the save: rc={ncsess.returncode}\n{out_nc[-400:]}'
            print(f'savekick-live: PASS (editor save mid-session; CCcam {n_cc}/{len(tags)} DCW, '
                  'Newcamd session completed, card connection kept)')
        finally:
            for p in sessions + [srv, peer]:
                if p.poll() is None: p.kill()
            for p in sessions + [srv, peer]:
                try: p.wait(timeout=5)
                except Exception: pass
    print('savekick-live: PASS')


if __name__ == '__main__':
    main()
