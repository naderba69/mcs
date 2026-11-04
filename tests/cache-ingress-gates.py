#!/usr/bin/env python3
"""Exercise CSP early gates with local seeded peers and a web trace."""
import base64
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'

def port(kind):
    with socket.socket(socket.AF_INET, kind) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def stop(p):
    if p and p.poll() is None:
        p.terminate()
        try: p.wait(timeout=5)
        except subprocess.TimeoutExpired: p.kill(); p.wait(timeout=5)

with tempfile.TemporaryDirectory(prefix='mcs-csp-gate-') as folder:
    d = Path(folder)
    peer = d/'peer'
    subprocess.run(['cc','-O2','-std=gnu89','-o',str(peer),str(ROOT/'tests/cachepeer.c')], check=True)
    for name, stage, caid, expected in [
        ('isolated', 2, '1884', 'peer reputation isolation'),
        ('disabled', 3, '1884', 'configured peer disabled'),
        ('caid', 0, '0500', 'CAID not allowed by cache'),
    ]:
        ports=set()
        while len(ports)<3: ports.add(port(socket.SOCK_STREAM))
        hp,ccport,ncport=ports
        cp=port(socket.SOCK_DGRAM); pp=port(socket.SOCK_DGRAM)
        while pp==cp: pp=port(socket.SOCK_DGRAM)
        rep=d/'peers.txt'; rep.write_text(f'127.0.0.1:{pp} {stage} {stage * 20} seeded\n' if stage else '')
        cfg=d/'config'
        cfg.write_text(f'HTTP PORT: {hp}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {ccport}\nF: traceuser tracepass\nCACHE PORT: {cp}\nCACHE PEER: 127.0.0.1:{pp} {{ csp=1 }}\nPEER REPUTATION: ON\nPEER REPUTATION FILE: {rep}\nCACHE FILTER: OFF\n\n[test]\nCAID: {caid}\nPROVIDERS: 000000\nPORT: {ncport}\nENABLE CACHE: YES\nUSER: u1 p1\n')
        env={k:v for k,v in os.environ.items() if not k.startswith('CP_')}
        env.update(CP_PUSH_HASH='12345678', CP_PUSH_SID='1136', CP_PUSH_CAID='1884', CP_REPUSH_MS='100', CP_REPUSH_SAME='1')
        server=peerproc=None
        with (d/'peer.log').open('w+') as plog, (d/'server.log').open('w+') as slog:
            try:
                peerproc=subprocess.Popen(['stdbuf','-o0','-e0',str(peer),str(cp),str(pp),'11223366445566FF77889998AABBCC31','5'],cwd=d,env=env,stdout=plog,stderr=subprocess.STDOUT)
                server=subprocess.Popen(['stdbuf','-o0','-e0',str(BINARY),'-v','-C',str(cfg)],cwd=d,stdout=slog,stderr=subprocess.STDOUT)
                url=f'http://127.0.0.1:{hp}/user-trace'
                auth={'Authorization':'Basic '+base64.b64encode(b'test:isolated').decode()}
                def request(data=None):
                    with urllib.request.urlopen(urllib.request.Request(url,data=data,headers=auth),timeout=3) as response: return response.read().decode()
                until=time.monotonic()+20
                while time.monotonic()<until:
                    if peerproc.poll() is not None or server.poll() is not None: raise RuntimeError('child exited')
                    try:
                        page=request()
                        if 'advertised card' in (d/'peer.log').read_text(): break
                    except OSError: pass
                    time.sleep(.1)
                else: raise RuntimeError('peer not ready')
                csrf=re.search("name='csrf' value='([a-f0-9]{64})'",page).group(1)
                cid=re.search("<option value='(\\d+)'[^>]*>traceuser",page).group(1)
                request(urllib.parse.urlencode({'csrf':csrf,'action':'start','client':cid,'caid':'1884','provid':'000000','sid':'1136'}).encode())
                until=time.monotonic()+5
                while time.monotonic()<until:
                    page=request()
                    if f'CSP reply dropped: {expected};' in page: break
                    time.sleep(.1)
                else: raise AssertionError((name, page, (d/'server.log').read_text(errors='replace')))
                row=next(row for row in page.split('<tr>') if f'CSP reply dropped: {expected};' in row)
                ar = {'isolated':'لعزل النظير في السمعة', 'disabled':'لأن النظير معطّل', 'caid':'CAID غير مسموح في الكاش'}
                assert ar[name] in row and 'header unverified' in row and 'قناة فقط، ليس ربط عميل' in row and 'data-event=' not in row, row
                print('PASS:',name,expected)
            finally:
                stop(server);stop(peerproc)
