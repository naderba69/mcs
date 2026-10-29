#!/usr/bin/env python3
"""Isolated CSP + CCcam trace, no production services or credentials."""
import base64
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'

def port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

def stop(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(timeout=5)

with tempfile.TemporaryDirectory(prefix='mcs-cache-trace-') as folder:
    tmp = pathlib.Path(folder)
    peer, client = tmp/'peer', tmp/'client'
    subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(peer), str(ROOT/'tests/cachepeer.c')], check=True)
    subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(client), str(ROOT/'tests/ccdcw.c')], check=True)
    for name, policy, verdict in [
        ('filter', 'CACHE FILTER: ON\n', 'cache not forwarded: filter did not establish cycle'),
        ('consensus', 'CACHE PROTECTION: STANDARD\nCACHE FILTER: OFF\n', 'cache held: consensus awaiting corroboration'),
        ('accepted', 'CACHE FILTER: OFF\n', 'cache forwarded CW to ECM pipe; delivery not confirmed'),
    ]:
        hp, ccport, ncport = [port(socket.SOCK_STREAM) for _ in range(3)]
        cp, pp = [port(socket.SOCK_DGRAM) for _ in range(2)]
        cfg = tmp/'config'
        cfg.write_text(f'HTTP PORT: {hp}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {ccport}\nF: traceuser tracepass\nCACHE PORT: {cp}\nCACHE PEER: 127.0.0.1:{pp} {{ csp=1 }}\n'+policy+f'\n[test]\nCAID: 1884\nPROVIDERS: 000000\nPORT: {ncport}\nDCW HALFNULLED: NO\nDCW CHECK: YES\nDCW MINTIME: 0\nDCWFILTER PATTERN: OFF\nENABLE CACHE: YES\nCACHE STATIC: NO\nSERVER TIMEOUT: 5000\nUSER: traceuser tracepass\n')
        with (tmp/'peer.log').open('w+') as plog, (tmp/'server.log').open('w+') as slog:
            peerproc = subprocess.Popen(['stdbuf','-o0','-e0',str(peer),str(cp),str(pp),'11223366445566FF77889998AABBCC31','5'], cwd=tmp, stdout=plog, stderr=subprocess.STDOUT)
            server = None
            try:
                server = subprocess.Popen(['stdbuf','-o0','-e0',str(BINARY),'-v','-C',str(cfg)], cwd=tmp, stdout=slog, stderr=subprocess.STDOUT)
                url = f'http://127.0.0.1:{hp}/user-trace'
                auth = {'Authorization':'Basic '+base64.b64encode(b'test:isolated').decode()}
                def request(data=None):
                    with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=auth), timeout=2) as response:
                        return response.read().decode()
                deadline = time.monotonic()+20
                while time.monotonic() < deadline:
                    if server.poll() is not None or peerproc.poll() is not None: raise RuntimeError('child exited')
                    try:
                        page = request()
                        if 'advertised card' in (tmp/'peer.log').read_text(): break
                    except OSError: pass
                    time.sleep(.1)
                else: raise RuntimeError('server or peer not ready')
                csrf = re.search("name='csrf' value='([a-f0-9]{64})'", page).group(1)
                cid = re.search("<option value='(\\d+)'[^>]*>traceuser", page).group(1)
                request(urllib.parse.urlencode({'csrf':csrf,'action':'start','client':cid,'caid':'1884','provid':'000000','sid':'1136'}).encode())
                result = subprocess.run([str(client),'127.0.0.1',str(ccport),'traceuser','tracepass','1884','1136','0x80','0x81'], capture_output=True, text=True, timeout=30, check=True)
                page = request()
                assert 'got request sid 1136' in (tmp/'peer.log').read_text(), name
                assert 'cache CW entered validation; decision pending' in page, (name, page)
                assert verdict in page, (name, page)
                assert 'قناة فقط، ليس ربط عميل' in page, name
                assert '11223366445566FF77889998AABBCC31' in page, name
                if name == 'accepted':
                    assert 'ECM1 tag=80 DCW' in result.stdout, result.stdout
                print('PASS:', name, verdict)
            finally:
                stop(server); stop(peerproc)
