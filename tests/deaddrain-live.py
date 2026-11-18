#!/usr/bin/env python3
"""Band 97 proof: one dead outbound server must not freeze a save.

Field data (operator's /user-trace journal): a browser save's
reload took 122116 ms, of which drain was 121886 ms -- the reload
waited in server_connect_wait_idle for ONE outbound connect
worker stuck in CreateClientSockTcp, a blocking connect() with
no timeout, waiting out the kernel's full SYN retry budget
against a dead/firewalled host. The config write lock is held
across the drain, so every client and the web froze with it.

Fix (two proven causes, one drain):
1. The worker now connects with CreateClientSockTcp_timeout (10 s)
   instead of the unbounded blocking connect.
2. recv_msg_thread took prg.locksrv and then ecm_lock() -- i.e. it
   waited for the config read gate while holding the very mutex the
   connect worker needs to publish its result. Live futex/owner
   proof: Config (writer, in wait_idle) -> worker (waiting locksrv)
   -> ECM Thread (locksrv owner, waiting on the config gate) -- a
   permanent cycle once a worker was mid-connect at save time.
   Both ECM-loop variants now pin+lock through
   newcamd_config_read_lock_mutex, the helper built for this pair.

This test points an N: line at a blackhole address (10.255.255.1
-- SYNs get no reply), lets the worker get stuck, then saves
through the editor and requires the reload to finish in a
fraction of the old budget, journaled with its phases.
"""
import base64, http.client, re, socket, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
AUTH = {'Authorization': 'Basic ' + base64.b64encode(b'admin:admin').decode()}


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def get(port, path):
    c = http.client.HTTPConnection('127.0.0.1', port, timeout=20)
    c.request('GET', path, headers=AUTH)
    r = c.getresponse(); b = r.read().decode('utf-8', 'replace'); c.close()
    return r.status, b


def main():
    with tempfile.TemporaryDirectory(prefix='mcs-deaddrain-') as td:
        tmp = Path(td)
        http_port, newcamd = free_port(), free_port()
        cfg = tmp / 'one.cfg'
        cfg.write_text(
            f'HTTP PORT: {http_port}\nHTTP USER: admin\nHTTP PASS: admin\nTELNET PORT: 0\n'
            f'N: 10.255.255.1 12000 deadsrv deadpass 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n'
            f'\n[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n'
            f'USER: u1 p1\n')
        srv = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(cfg)],
                               cwd=tmp, stdout=open(tmp / 'srv.log', 'w'), stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 20
            while True:
                try:
                    st, _ = get(http_port, '/'); break
                except Exception:
                    if time.monotonic() > deadline: raise AssertionError('server never came up')
                    time.sleep(0.3)
            time.sleep(3)  # the connect worker is now stuck in the blackhole

            body = cfg.read_bytes() + b'\n# operator save with a dead server\n'
            b = '----WebKitFormBoundary7MA4YWxkTrZu0gW'
            payload = (f'--{b}\r\nContent-Disposition: form-data; name="textedit"\r\n\r\n').encode() \
                + body + f'\r\n--{b}--\r\n'.encode()
            t0 = time.monotonic()
            conn = http.client.HTTPConnection('127.0.0.1', http_port, timeout=120)
            conn.request('POST', '/editor0', body=payload, headers={
                'Content-Type': f'multipart/form-data; boundary={b}', **AUTH})
            resp = conn.getresponse(); page = resp.read().decode(errors='replace')
            assert resp.status == 200 and 'Successfully Saved' in page, 'editor save failed'

            total = None; phases = ''
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                try:
                    st, trace = get(http_port, '/user-trace')
                except Exception:
                    time.sleep(0.5); continue  # web is expected to stall during the drain
                m = re.search(r'took (\d+) ms \(drain (\d+)ms, parse (\d+)ms, apply (\d+)ms\)', trace)
                if m:
                    total = int(m.group(1)); phases = m.group(0); break
                time.sleep(0.5)
            assert total is not None, 'reload never finished within 60s (journal shows no reload-end)'
            elapsed = time.monotonic() - t0
            assert total < 40000, f'reload still froze the server: {total} ms ({phases})'
            print(f'deaddrain-live: reload with a stuck dead-server worker: {total} ms ({phases}), '
                  f'save-to-journal {elapsed:.1f}s')
            assert srv.poll() is None, 'server died'
        finally:
            srv.kill(); srv.wait(timeout=5)
    print('deaddrain-live: PASS')


if __name__ == '__main__':
    main()
