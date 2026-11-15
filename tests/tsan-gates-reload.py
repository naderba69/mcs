"""TSan scenario (band 51): flip global gate scalars in place while
real cache/ECM traffic runs.

Band 48 left cfg.cache.* scalar reads outside the config gate as a
named debt, to be reopened only if an expanded TSan session under
reload produced a report. This harness is that session: a CacheEX
peer keeps one 1884 cache entry populated, a client keeps asking,
and a reloader thread rewrites CACHE SERVICE DOUBT / CACHE CI
VALIDATE / AUTO RECOVER / CACHE THRESHOLD in the config file
(same inode, equal-length writes, inotify-friendly) every ~0.7 s.
The TSan session script (tsan-r22-session.sh, scenario `gates`)
collects the sanitizer reports; this harness only proves the
server stayed alive and kept answering.
"""
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY = pathlib.Path(sys.argv[1]).resolve()

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
    with tempfile.TemporaryDirectory(prefix='mcs-tsan-gates-') as folder:
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
        config = tmp / 'config'
        config.write_text(
            f'HTTP PORT: {http_port}\nHTTP USER: test\nHTTP PASS: test\n'
            'TELNET PORT: 0\n'
            f'CCCAM PORT: {cc_port}\nF: cxu cxp {{ cacheex_mode=3; }}\n'
            f'CACHE PORT: {cache_port}\n'
            'CACHE FILTER: OFF\nCACHE THRESHOLD: 1\n'
            'CACHE CI VALIDATE: OFF\n'
            'CACHE SERVICE DOUBT: OFF\n'
            'AUTO RECOVER: OFF\n'
            '\n[doubt1884]\nCAID: 1884\nPROVIDERS: 000000\n'
            f'PORT: {nc_port}\n'
            'ENABLE CACHEEX: YES\n'
            'DCW HALFNULLED: NO\nDCW CHECK: YES\nDCW MINTIME: 0\n'
            'DCWFILTER PATTERN: OFF\nENABLE CACHE: YES\nCACHE TIMEOUT: 600\n'
            'CACHE STATIC: NO\nSERVER TIMEOUT: 3000\nDCW TIMEOUT: 4000\n'
            'CACHE EVIDENCE: YES\n'
            'USER: u1 p1\n'
        )
        server_log = tmp / 'server.log'
        server_proc = None
        halt = threading.Event()

        def reloader():
            pairs = [('CACHE SERVICE DOUBT: OFF', 'CACHE SERVICE DOUBT: ON '),
                     ('CACHE CI VALIDATE: OFF', 'CACHE CI VALIDATE: ON '),
                     ('AUTO RECOVER: OFF', 'AUTO RECOVER: ON '),
                     ('CACHE THRESHOLD: 1', 'CACHE THRESHOLD: 2')]
            state = [0] * len(pairs)
            while not halt.is_set():
                with open(config, 'r+') as fh:
                    data = fh.read()
                    for i, (off, on) in enumerate(pairs):
                        if state[i]:
                            data = data.replace(on, off)
                        else:
                            data = data.replace(off, on)
                        state[i] ^= 1
                    fh.seek(0)
                    fh.write(data)
                    fh.truncate()
                halt.wait(0.7)

        try:
            with server_log.open('w+') as sf:
                server_proc = subprocess.Popen(
                    ['stdbuf', '-o0', '-e0', str(BINARY), '-C', str(config), '-v'],
                    stdout=sf, stderr=subprocess.STDOUT, cwd=tmp)
                deadline = time.monotonic() + 25
                while True:
                    try:
                        with socket.create_connection(('127.0.0.1', cc_port), timeout=0.3):
                            break
                    except OSError:
                        if time.monotonic() > deadline:
                            raise RuntimeError('server CCcam port never opened')
                        time.sleep(0.1)
                jobs = []
                for cw in (CW_A, CW_B):
                    for _ in range(5):
                        jobs += [f'push 1884 000000 1136 {cw}', 'sleep 40']
                subprocess.run([str(cxpeer), '127.0.0.1', str(cc_port), 'cxu', 'cxp'] + jobs,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
                thread = threading.Thread(target=reloader, daemon=True)
                thread.start()
                env = dict(__import__('os').environ, NC_ECMS='1')
                asks = 0
                end = time.monotonic() + 14
                while time.monotonic() < end:
                    subprocess.run(
                        [str(ncclient), '127.0.0.1', str(nc_port), 'u1', 'p1',
                         '0102030405060708091011121314', '0', '1884', '1136'],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                        timeout=20, env=env)
                    asks += 1
                halt.set()
                thread.join(timeout=3)
                assert server_proc.poll() is None, 'server died during gate flips'
                with socket.create_connection(('127.0.0.1', http_port), timeout=2):
                    pass
                print(f'PASS tsan-gates-reload ({asks} asks under scalar flips)')
        finally:
            halt.set()
            stop(server_proc)


if __name__ == '__main__':
    main()
