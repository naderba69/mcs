#!/usr/bin/env python3
"""Band 79 live proof: the HTTP ban lives in the unified protoban table.

Before band 79 the web door kept a private ban table (band 57 lt
slots): two tables, two status counters, and protoban_unban could not
lift a web ban. Now HTTP mirrors telnet exactly -- lt counts only for
the progressive delay, PB_HTTP carries the ban -- so /user-trace
shows web bans in the one per-protocol list and the web-guard line
reads the same table.

Phase A (armed 3/4s): three wrong-password requests ban 127.0.0.1;
the CORRECT password is then refused pre-auth from that IP, while
the same page fetched from 127.0.0.2 shows 'http -- 127.0.0.1' in the
protoban list and محظور الآن: 1 on the guard line. After the ban
expires the correct login works and the guard line keeps the total.
Phase B (unarmed): wrong attempts change nothing (stock).
"""
import base64, http.client, re, socket, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def get(port, src, userpw):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=5, source_address=(src, 0))
    try:
        auth = 'Basic ' + base64.b64encode(userpw.encode()).decode()
        conn.request('GET', '/user-trace', headers={'Authorization': auth})
        resp = conn.getresponse()
        return resp.status, resp.read().decode('utf-8', errors='replace')
    except (http.client.RemoteDisconnected, ConnectionResetError, BrokenPipeError, TimeoutError, OSError):
        return 0, ''   # closed pre-auth: the ban door
    finally:
        conn.close()


def boot(armed):
    http, pport = free_port(), free_port()
    td = tempfile.TemporaryDirectory(prefix='mcs-hban-')
    work = Path(td.name)
    lines = [f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin', 'TELNET PORT: 0']
    if armed:
        lines += ['HTTP LOGIN FAILS: 3', 'HTTP LOGIN BAN: 4']
    lines += ['', '[ hban ]', f'PORT: {pport}', 'CAID: 1884', 'PROVIDERS: 0', 'USER: u1 p1', '']
    (work / 'one.cfg').write_text('\n'.join(lines))
    srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'one.cfg')],
                            stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
    deadline = time.monotonic() + 15
    while True:
        st, _ = get(http, '127.0.0.1', 'admin:admin')
        if st == 200: break
        if time.monotonic() > deadline: raise AssertionError('server never came up')
        time.sleep(0.3)
    return td, srvp, http


def main():
    td, srvp, http = boot(armed=True)
    try:
        for i in range(3):
            st, _ = get(http, '127.0.0.1', 'admin:wrong')
            assert st == 401, f'wrong attempt {i+1}: expected 401, got {st}'
        st, _ = get(http, '127.0.0.1', 'admin:admin')
        assert st == 0, f'banned IP must be closed pre-auth even with correct creds, got {st}'
        st, page = get(http, '127.0.0.2', 'admin:admin')
        assert st == 200, f'other source IP must stay served, got {st}'
        assert re.search(r'http — 127\.0\.0\.1', page), 'http ban missing from unified protoban list'
        assert 'محظور الآن: 1' in page, 'web guard line does not read the unified table'
        print('httpban-live: phase A1 PASS (ban enforced pre-auth, visible in unified table)')
        time.sleep(5)
        st, page = get(http, '127.0.0.1', 'admin:admin')
        assert st == 200, f'correct login after expiry must work, got {st}'
        assert 'حظر إجمالي هذه الجلسة: 1' in page and 'محظور الآن: 0' in page, \
            'guard line lost the ban total after expiry'
        print('httpban-live: phase A2 PASS (expiry lifts ban, total kept)')
    finally:
        srvp.kill(); srvp.wait(timeout=5); td.cleanup()

    td, srvp, http = boot(armed=False)
    try:
        for i in range(3):
            st, _ = get(http, '127.0.0.1', 'admin:wrong')
            assert st == 401
        st, _ = get(http, '127.0.0.1', 'admin:admin')
        assert st == 200, 'unarmed web door must keep stock behavior'
        print('httpban-live: phase B PASS (unarmed = stock)')
    finally:
        srvp.kill(); srvp.wait(timeout=5); td.cleanup()
    print('httpban-live: PASS')


if __name__ == '__main__':
    main()
