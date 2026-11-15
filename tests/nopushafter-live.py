"""CACHEEX NOPUSH AFTER live proof (band 41).

A slow synthetic card (1200 ms) answers 1884:000000:1136 while a CSP
peer watches. Control run (no NOPUSH line): the committed CW is
broadcast to the peer (it logs an unsolicited TYPE_REPLY). Armed run
(CACHEEX NOPUSH AFTER: 1884:500): the client still receives the CW,
but the peer receives no push.
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
MODE = os.environ.get("NOPUSH_MODE", "armed")


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


def build(tmp, name, srcs, extra_env=None):
    out = tmp / name
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    subprocess.run(srcs + ['-o', str(out)], check=True, env=env)
    return out


def main():
    with tempfile.TemporaryDirectory(prefix='mcs-nopush-') as folder:
        tmp = pathlib.Path(folder)
        peer = build(tmp, 'peer', ['cc', '-O2', '-std=gnu89', '-w', str(ROOT / 'tests/cachepeer.c')])
        ncserver = build(tmp, 'ncserver', ['cc', '-O2', '-std=gnu89', '-w', str(ROOT / 'tests/ncserver.c'),
                                           str(ROOT / 'src/des.c'), str(ROOT / 'src/msg-newcamd.c'), str(ROOT / 'src/md5.c')])
        ncclient = build(tmp, 'ncclient', ['cc', '-O2', '-std=gnu89', '-w', str(ROOT / 'tests/ncclient.c'),
                                           str(ROOT / 'src/des.c'), str(ROOT / 'src/msg-newcamd.c'), str(ROOT / 'src/md5.c')])

        http_port = free_port(socket.SOCK_STREAM)
        nc_port = free_port(socket.SOCK_STREAM)
        cc_port = free_port(socket.SOCK_STREAM)
        card_port = free_port(socket.SOCK_STREAM)
        cache_port = free_port(socket.SOCK_DGRAM)
        peer_port = free_port(socket.SOCK_DGRAM)
        gate = 'CACHEEX NOPUSH AFTER: 1884:500\n' if MODE == 'armed' else ''
        config = tmp / 'config'
        config.write_text(
            f'HTTP PORT: {http_port}\nHTTP USER: test\nHTTP PASS: test\n'
            'TELNET PORT: 0\n'
            f'CCCAM PORT: {cc_port}\n'
            f'CACHE PORT: {cache_port}\n'
            f'CACHE PEER: 127.0.0.1:{peer_port} {{ csp=1 }}\n'
            'CACHE FILTER: OFF\n'
            f'{gate}'
            f'N: 127.0.0.1 {card_port} card pass 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n'
            '\n[slow1884]\nCAID: 1884\nPROVIDERS: 000000\n'
            f'PORT: {nc_port}\n'
            'DCW HALFNULLED: NO\nDCW CHECK: YES\nDCW MINTIME: 0\n'
            'DCWFILTER PATTERN: OFF\nENABLE CACHE: YES\nCACHE TIMEOUT: 300\n'
            'CACHE STATIC: NO\nSERVER TIMEOUT: 5000\nDCW TIMEOUT: 6000\n'
            'USER: u1 p1\n'
        )
        # peer CW has a broken checksum byte: front must reject it and
        # fall through to the slow card, so the commit is genuinely late.
        peer_cw = '11223367445566FF77889998AABBCC31'
        card_cw = '11223366445566FF77889998AABBCC31'
        peer_log = tmp / 'peer.log'
        server_log = tmp / 'server.log'
        card_log = tmp / 'card.log'
        with peer_log.open('w+') as pf, server_log.open('w+') as sf, card_log.open('w+') as cf:
            peer_proc = subprocess.Popen(
                ['stdbuf', '-o0', '-e0', str(peer), str(cache_port), str(peer_port), peer_cw, '50', '18840000'],
                stdout=pf, stderr=subprocess.STDOUT, cwd=tmp)
            card_env = dict(os.environ, NS_DELAYS='1200', NS_CW_SEQUENCE=card_cw)
            card_proc = subprocess.Popen(
                ['stdbuf', '-o0', '-e0', str(ncserver), str(card_port),
                 '0102030405060708091011121314', '1884', '000000'],
                stdout=cf, stderr=subprocess.STDOUT, cwd=tmp, env=card_env)
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
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if 'caid 1884 providers' in server_log.read_text(errors='replace'):
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError('card server never connected')
                r = subprocess.run(
                    [str(ncclient), '127.0.0.1', str(nc_port), 'u1', 'p1',
                     '0102030405060708091011121314', '0', '1884', '1136'],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=40)
                delivered = r.stdout.count('DELIVERED A CONTROL WORD')
                assert delivered >= 1, ('client did not receive the late CW', r.stdout)
                time.sleep(1.0)
                peer_out = peer_log.read_text(errors='replace')
                pushes = peer_out.count('ignored packet type 2 ')
                server_out = server_log.read_text(errors='replace')
                if MODE == 'armed':
                    assert 'CACHEEX NOPUSH AFTER: 1 entries' in server_out, 'gate not parsed'
                    assert pushes == 0, (f'late CW was pushed despite NOPUSH AFTER ({pushes})', peer_out)
                    print('PASS nopushafter-live (late CW delivered to client, not pushed)')
                else:
                    assert pushes >= 1, ('control run: expected a broadcast push to the peer', peer_out)
                    print('PASS nopushafter-live control (late CW pushed without the gate)')
            finally:
                stop(server_proc)
                stop(card_proc)
                stop(peer_proc)


if __name__ == '__main__':
    main()
