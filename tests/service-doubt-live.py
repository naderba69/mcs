"""CACHE SERVICE DOUBT live proof (band 42).

A CacheEX peer (tests/cxpeer.c, which builds the ECM exactly as
tests/ncclient.c does) pushes two distinct checksum-valid CWs for
one 1884 ECM into the cache, then disconnects. A single fresh client
ECM must then be decided by the stored-cache FIND selection
(profile CACHE EVIDENCE ranking):
  - doubt mode    (A x5, B x5, gate ON): no strict peer majority ->
    the client must NOT be served from cache (svc_doubt counter).
  - majority mode (A x6, B x5, gate ON): strict majority ->
    the client IS served (svc_majority counter).
  - control       (gate OFF, A x5, B x5): the client IS served and
    the doubt counters stay at zero.
  - prof-on       (global OFF, profile SERVICE DOUBT: ON, band 43):
    the tied client must NOT be served.
  - prof-off      (global ON, profile SERVICE DOUBT: OFF, band 43):
    the tied client IS served and the counters stay at zero.
No card servers exist, so any delivery is a cache serve. The peer
counters are the candidate arrival counts (nbpeers), which is the
unit the selector itself tallies.
"""
import base64
import http.client
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
MODE = os.environ.get("SVC_MODE", "doubt")

CW_A = '11223366445566FF77889998AABBCC31'
CW_B = '22334499556677328899AACBBBCCDD64'


def free_port(t):
    with socket.socket(socket.AF_INET, t) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def stop(p):
    if p and p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait(timeout=3)


def main():
    with tempfile.TemporaryDirectory(prefix='mcs-svcdoubt-') as folder:
        tmp = pathlib.Path(folder)
        cxpeer = tmp / 'cxpeer'
        ncclient = tmp / 'ncclient'
        subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(cxpeer),
                        str(ROOT / 'tests/cxpeer.c')], check=True)
        subprocess.run(['cc', '-O2', '-std=gnu89', '-w', '-o', str(ncclient),
                        str(ROOT / 'tests/ncclient.c'), str(ROOT / 'src/des.c'),
                        str(ROOT / 'src/msg-newcamd.c'), str(ROOT / 'src/md5.c')], check=True)

        http_port = free_port(socket.SOCK_STREAM)
        nc_port = free_port(socket.SOCK_STREAM)
        cc_port = free_port(socket.SOCK_STREAM)
        cache_port = free_port(socket.SOCK_DGRAM)
        counts = {'doubt': (5, 5), 'majority': (6, 5), 'control': (5, 5), 'prof-on': (5, 5), 'prof-off': (5, 5)}[MODE]
        gate = 'CACHE SERVICE DOUBT: ON\n' if MODE in ('doubt', 'majority', 'prof-off') else 'CACHE SERVICE DOUBT: OFF\n'
        profile_gate = {'prof-on': 'SERVICE DOUBT: ON\n', 'prof-off': 'SERVICE DOUBT: OFF\n'}.get(MODE, '')
        config = tmp / 'config'
        config.write_text(
            f'HTTP PORT: {http_port}\nHTTP USER: test\nHTTP PASS: test\n'
            'TELNET PORT: 0\n'
            f'CCCAM PORT: {cc_port}\nF: cxu cxp {{ cacheex_mode=3; }}\n'
            f'CACHE PORT: {cache_port}\n'
            'CACHE FILTER: OFF\nCACHE THRESHOLD: 1\nAUTO RECOVER: ON\n'
            f'{gate}'
            '\n[doubt1884]\nCAID: 1884\nPROVIDERS: 000000\n'
            f'PORT: {nc_port}\n'
            'ENABLE CACHEEX: YES\n'
            'DCW HALFNULLED: NO\nDCW CHECK: YES\nDCW MINTIME: 0\n'
            'DCWFILTER PATTERN: OFF\nENABLE CACHE: YES\nCACHE TIMEOUT: 600\n'
            'CACHE STATIC: NO\nSERVER TIMEOUT: 3000\nDCW TIMEOUT: 4000\n'
            'CACHE EVIDENCE: YES\n'
            f'{profile_gate}'
            'USER: u1 p1\n'
        )
        server_log = tmp / 'server.log'
        server_proc = None
        try:
            with server_log.open('w+') as sf:
                server_proc = subprocess.Popen(
                    ['stdbuf', '-o0', '-e0', str(BINARY), '-C', str(config), '-v'],
                    stdout=sf, stderr=subprocess.STDOUT, cwd=tmp)
                deadline = time.monotonic() + 20
                while True:
                    try:
                        with socket.create_connection(('127.0.0.1', cc_port), timeout=0.3):
                            break
                    except OSError:
                        if time.monotonic() > deadline:
                            raise RuntimeError('server CCcam port never opened')
                        time.sleep(0.1)
                jobs = []
                for cw, n in ((CW_A, counts[0]), (CW_B, counts[1])):
                    for _ in range(n):
                        jobs += [f'push 1884 000000 1136 {cw}', 'sleep 60']
                peer = subprocess.run(
                    [str(cxpeer), '127.0.0.1', str(cc_port), 'cxu', 'cxp'] + jobs,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
                assert 'push ch 1884:000000:1136' in peer.stdout, peer.stdout
                time.sleep(3.0)

                env = dict(os.environ, NC_ECMS='1')
                r = subprocess.run(
                    [str(ncclient), '127.0.0.1', str(nc_port), 'u1', 'p1',
                     '0102030405060708091011121314', '0', '1884', '1136'],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                    timeout=40, env=env)
                delivered = r.stdout.count('DELIVERED A CONTROL WORD')

                conn = http.client.HTTPConnection('127.0.0.1', http_port, timeout=5)
                auth = base64.b64encode(b'test:test').decode()
                conn.request('GET', '/user-trace', headers={'Authorization': 'Basic ' + auth})
                page = conn.getresponse().read().decode('utf-8', 'replace')
                assert 'الاسترداد التلقائي (مراقبة فقط' in page, 'autorecov monitor line missing from /user-trace'
                assert 'بدء استرداد: 0' in page, 'autorecov counters wrong'
                assert 'البوابات المسلّحة' in page, 'armed-gates summary missing from /user-trace'
                assert 'CACHE EVIDENCE في 1 بروفايل' in page, 'armed-gates summary wrong: ' + page[page.find('البوابات'):page.find('البوابات')+300]
                i = page.find('CACHE SERVICE DOUBT')
                counters = page[max(0, i - 60):i + 220] if i >= 0 else 'PAGE SECTION MISSING'
                server_out = server_log.read_text(errors='replace')
                if MODE == 'doubt':
                    assert delivered == 0, ('tied CWs were served despite SERVICE DOUBT', server_out[-1500:])
                    assert 'رُفضت الخدمة للشك: 1' in counters, counters
                    print('PASS service-doubt-live doubt (tied CWs not served)')
                elif MODE == 'majority':
                    assert delivered >= 1, ('strict-majority CW was not served', server_out[-1500:])
                    assert 'خُدمت بأغلبية صارمة: 1' in counters, counters
                    print('PASS service-doubt-live majority (strict majority served)')
                elif MODE == 'prof-on':
                    assert delivered == 0, ('profile SERVICE DOUBT: ON did not refuse the tie', server_out[-1500:])
                    assert 'رُفضت الخدمة للشك: 1' in counters, counters
                    print('PASS service-doubt-live prof-on (profile ON overrides global OFF)')
                elif MODE == 'prof-off':
                    assert delivered >= 1, ('profile SERVICE DOUBT: OFF did not serve the tie', server_out[-1500:])
                    assert 'انتقادات ذات مرشحين مختلفين: 0' in counters, counters
                    print('PASS service-doubt-live prof-off (profile OFF overrides global ON)')
                else:
                    assert delivered >= 1, ('control: cache serve missing with gate OFF', server_out[-1500:])
                    assert 'انتقادات ذات مرشحين مختلفين: 0' in counters, counters
                    print('PASS service-doubt-live control (gate OFF serves)')
        finally:
            stop(server_proc)


if __name__ == '__main__':
    main()
