#!/usr/bin/env python3
"""Band 95 proof: a browser save must not kick clients when the
card the source actually advertised differs from the PROVIDERS
line written in the file.

Production shape (the operator's repeated report): profiles
declare `PROVIDERS: 0` (or a partial list) while the real card
behind the N: line advertises its own provider set. The reload
matcher compared the LIVE learned card against the file's
declaration: subset difference -> FLAG_DISCONNECT (check_config
then drops every client of the profile), disjoint difference ->
no match at all (profile deleted and re-added, clients dropped
with it). Either way every editor save churned every client of
every such profile: "it stopped and wants to connect". The
learned card is the truth; the reload must keep it and keep the
sessions. This test is savekick-live with the provider mismatch
that production actually has.
"""
import base64, http.client, os, socket, subprocess, sys, tempfile, time
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
    with tempfile.TemporaryDirectory(prefix='mcs-savereload-') as td:
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
        # the card advertises provider 0064; the file declares PROVIDERS: 0
        peer = subprocess.Popen([str(TESTS / '.ncserver.bin'), str(card), KEY, '1884', '0064'],
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
            tags = [f'0x{0x80 + i % 2:02x}' for i in range(30)]
            ccsess = subprocess.Popen([str(cc), '127.0.0.1', str(cccam), 'traceuser', 'tracepass',
                                       '1884', '1136'] + tags,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            ncsess = subprocess.Popen([str(TESTS / '.ncclient.bin'), '127.0.0.1', str(newcamd),
                                       'traceuser', 'tracepass', KEY, '0', '1884', '0064'],
                                      env=dict(os.environ, NC_ECMS='30', PATH='/usr/bin:/bin'),
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            sessions = [ccsess, ncsess]
            time.sleep(3)  # both sessions mid-flight

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
                f'CCcam session kicked by save+reload (learned-card mismatch): ' \
                f'rc={ccsess.returncode} DCW {n_cc}/{len(tags)}\n{out_cc[-400:]}'
            assert 'CONTROL WORD' in out_nc and ncsess.returncode == 0, \
                f'Newcamd session kicked by save+reload (learned-card mismatch): ' \
                f'rc={ncsess.returncode}\n{out_nc[-400:]}'
            print(f'savereload-live: PASS (save+reload with PROVIDERS mismatch; '
                  f'CCcam {n_cc}/{len(tags)} DCW, Newcamd session completed)')
        finally:
            for p in sessions + [srv, peer]:
                if p.poll() is None: p.kill()
            for p in sessions + [srv, peer]:
                try: p.wait(timeout=5)
                except Exception: pass
    print('savereload-live: PASS')


if __name__ == '__main__':
    main()
