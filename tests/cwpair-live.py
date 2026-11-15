"""CWPAIR live proof (band 38): a CW produced for the simulcrypt twin
183D is served to a source-less 183E request on total miss.

The cache peer answers 183D with a checksum-valid CW. A Newcamd client
takes it on 183D (commit feeds pair observation). A second client then
asks 183E (mapped twin, no servers at all): after the DCW timeout the
pair lookup must serve the same CW. With CWPAIR ACTIVE: NO (control
run via CWPAIR_MODE=measure) the 183E request must fail instead.
"""
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
MODE = os.environ.get("CWPAIR_MODE", "active")


def free_port(sock_type):
    with socket.socket(socket.AF_INET, sock_type) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def stop(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


def main():
    with tempfile.TemporaryDirectory(prefix='mcs-cwpair-') as folder:
        tmp = pathlib.Path(folder)
        peer = tmp / 'peer'
        ncclient = tmp / 'ncclient'
        subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(peer),
                        str(ROOT / 'tests/cachepeer.c')], check=True)
        subprocess.run(['cc', '-O2', '-std=gnu89', '-w', '-o', str(ncclient),
                        str(ROOT / 'tests/ncclient.c'), str(ROOT / 'src/des.c'),
                        str(ROOT / 'src/msg-newcamd.c'), str(ROOT / 'src/md5.c')], check=True)

        http_port = free_port(socket.SOCK_STREAM)
        nc_twin = free_port(socket.SOCK_STREAM)
        nc_pair = free_port(socket.SOCK_STREAM)
        cc_port = free_port(socket.SOCK_STREAM)
        cache_port = free_port(socket.SOCK_DGRAM)
        peer_port = free_port(socket.SOCK_DGRAM)
        active = "YES" if MODE == "active" else "NO"
        config = tmp / 'config'
        config.write_text(
            f'HTTP PORT: {http_port}\nHTTP USER: test\nHTTP PASS: test\n'
            'TELNET PORT: 0\n'
            f'CCCAM PORT: {cc_port}\n'
            f'CACHE PORT: {cache_port}\n'
            f'CACHE PEER: 127.0.0.1:{peer_port} {{ csp=1 }}\n'
            'CACHE FILTER: OFF\n'
            'CWDEDUP CHECK: YES\n'
            'CWPAIR DISCOVER CAID: 183D,183E\n'
            '\n[twin183d]\nCAID: 183D\nPROVIDERS: 000000\n'
            f'PORT: {nc_twin}\n'
            'DCW HALFNULLED: NO\nDCW CHECK: YES\nDCW MINTIME: 0\n'
            'DCWFILTER PATTERN: OFF\nENABLE CACHE: YES\nCACHE TIMEOUT: 400\n'
            'SERVER TIMEOUT: 3000\nDCW TIMEOUT: 4000\n'
            'USER: u1 p1\n'
            '\n[pair183e]\nCAID: 183E\nPROVIDERS: 000000\n'
            f'PORT: {nc_pair}\n'
            'DCW HALFNULLED: NO\nDCW CHECK: YES\nDCW MINTIME: 0\n'
            'DCWFILTER PATTERN: OFF\nENABLE CACHE: YES\nCACHE TIMEOUT: 400\n'
            'SERVER TIMEOUT: 3000\nDCW TIMEOUT: 2500\n'
            'CWPAIR CAID: 183D\n'
            f'CWPAIR ACTIVE: {active}\n'
            'CWPAIR MAXAGE: 21000\n'
            'CWPAIR SID: 0064->0064\n'
            'USER: u1 p1\n'
        )
        # checksum-valid CW (same pattern the 1884 harness uses)
        cw = '11223366445566FF77889998AABBCC31'
        peer_log = tmp / 'peer.log'
        server_log = tmp / 'server.log'
        with peer_log.open('w+') as pf, server_log.open('w+') as sf:
            peer_proc = subprocess.Popen(
                ['stdbuf', '-o0', '-e0', str(peer), str(cache_port), str(peer_port), cw, '5', '183D0000'],
                stdout=pf, stderr=subprocess.STDOUT, cwd=tmp)
            server_proc = None
            try:
                time.sleep(0.2)
                server_proc = subprocess.Popen(
                    ['stdbuf', '-o0', '-e0', str(BINARY), '-C', str(config), '-v'],
                    stdout=sf, stderr=subprocess.STDOUT, cwd=tmp)
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if 'advertised card' in peer_log.read_text(errors='replace'):
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError('cache peer handshake failed')

                def ask(port, caid, sid):
                    r = subprocess.run(
                        [str(ncclient), '127.0.0.1', str(port), 'u1', 'p1',
                         '0102030405060708091011121314', '0', caid, sid],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=40)
                    return r.stdout.count('DELIVERED A CONTROL WORD')

                twin_delivered = ask(nc_twin, '183D', '0064')
                assert twin_delivered >= 1, 'twin 183D itself was not delivered from cache'
                pair_delivered = ask(nc_pair, '183E', '0064')
                server_out = server_log.read_text(errors='replace')
                if MODE == "active":
                    assert pair_delivered >= 1, (
                        'paired 183E request not served\n' + server_out[-2000:])
                    assert '[CWPAIR]' in server_out, 'no CWPAIR serve line in log'
                    print('PASS cwpair-live (twin CW served to source-less simulcrypt channel)')
                else:
                    assert pair_delivered == 0, 'measure mode must not serve'
                    print('PASS cwpair-live measure mode (ACTIVE: NO serves nothing)')
            finally:
                stop(server_proc)
                stop(peer_proc)


if __name__ == '__main__':
    main()
