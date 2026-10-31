#!/usr/bin/env python3
"""Synthetic CSP conflict: equal-weight tie vs weighted loss and peer reputation."""
import base64
import contextlib
import re
import urllib.parse
import urllib.request
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parent.parent
BINARY=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'bin/multics.x64'
def port(kind):
    with socket.socket(socket.AF_INET,kind) as s:
        s.bind(('127.0.0.1',0)); return s.getsockname()[1]
def stop(p):
    if p and p.poll() is None:
        p.terminate()
        try:p.wait(timeout=4)
        except subprocess.TimeoutExpired:p.kill();p.wait(timeout=4)

with tempfile.TemporaryDirectory(prefix='mcs-three-peers-') as folder:
    d=Path(folder); peer=d/'peer'; client=d/'client'
    subprocess.run(['cc','-O2','-std=gnu89','-o',str(peer),str(ROOT/'tests/cachepeer.c')],check=True)
    subprocess.run(['cc','-O2','-std=gnu89','-w','-o',str(client),str(ROOT/'tests/ncclient.c'),str(ROOT/'src/des.c'),str(ROOT/'src/msg-newcamd.c'),str(ROOT/'src/md5.c')],check=True)
    # Delayed replies make the ordering deterministic on isolated local peers.
    for name, count in [('timing-tie',2),('timing-repeat',3),('weighted-loss',3)]:
        tcp=set()
        while len(tcp)<3:tcp.add(port(socket.SOCK_STREAM))
        np,hp,ccport=tcp; cp=port(socket.SOCK_DGRAM); peers=[]
        while len(peers)<count:
            p=port(socket.SOCK_DGRAM)
            if p!=cp and p not in peers:peers.append(p)
        cycle_rule = ''
        cfg=d/'config'; cfg.write_text(f'HTTP PORT: {hp}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {ccport}\nF: traceuser tracepass\nCACHE PORT: {cp}\nCACHE PROTECTION: STANDARD\nCACHE FILTER: OFF\nPEER REPUTATION: ON\nPEER REPUTATION DISTRUST: 1\n'+''.join(f'CACHE PEER: 127.0.0.1:{p} {{ csp=1; fwd=1 }}\n' for p in peers)+f'\n[test]\nCAID: 1884\nPROVIDERS: 000000\nPORT: {np}\n{cycle_rule}DCW CHECK: YES\nDCW HALFNULLED: NO\nDCW MINTIME: 0\nDCWFILTER PATTERN: OFF\nDCW TIMEOUT: 4000\nENABLE CACHE: YES\nCACHE STATIC: NO\nCACHE TIMEOUT: 900\nSERVER TIMEOUT: 4000\nUSER: u1 p1\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('CP_','NC_'))}
        procs=[]; server=None
        with contextlib.ExitStack() as stack:
            logs=[stack.enter_context((d/f'peer{i}.log').open('w+')) for i in range(count)]
            slog=stack.enter_context((d/'server.log').open('w+'))
            try:
                for i,(p,log) in enumerate(zip(peers,logs)):
                    peer_env=dict(env,CP_ANSWER_DELAY_MS=str(i*150))
                    key = '1234569C789ABCCECDEF12CE234567CF' if (i > 0 if name == 'timing-repeat' else i == count-1) else '11223366445566FF77889998AABBCC31'
                    procs.append(subprocess.Popen(['stdbuf','-o0','-e0',str(peer),str(cp),str(p),key,'5'],cwd=d,env=peer_env,stdout=log,stderr=subprocess.STDOUT))
                server=subprocess.Popen(['stdbuf','-o0','-e0',str(BINARY),'-C',str(cfg),'-v'],cwd=d,stdout=slog,stderr=subprocess.STDOUT)
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    if server.poll() is not None or any(p.poll() is not None for p in procs):raise RuntimeError('child exited')
                    if all('advertised card' in (d/f'peer{i}.log').read_text(errors='replace') for i in range(count)):break
                    time.sleep(.1)
                else:raise RuntimeError('peers not ready')
                url=f'http://127.0.0.1:{hp}/user-trace'
                auth={'Authorization':'Basic '+base64.b64encode(b'test:isolated').decode()}
                def request(data=None):
                    with urllib.request.urlopen(urllib.request.Request(url,data=data,headers=auth),timeout=5) as response:
                        return response.read().decode()
                page=request()
                csrf=re.search("name='csrf' value='([a-f0-9]{64})'",page).group(1)
                cid=re.search("<option value='(\\d+)'[^>]*>traceuser",page).group(1)
                request(urllib.parse.urlencode({'csrf':csrf,'action':'start','client':cid,'caid':'1884','provid':'000000','sid':'1136'}).encode())
                run=subprocess.run([str(client),'127.0.0.1',str(np),'u1','p1','0102030405060708091011121314','0','1884','1136'],env=dict(env,NC_ECMS='1'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
                counts=[(d/f'peer{i}.log').read_text(errors='replace').count('PEER: replied CW') for i in range(count)]
                serverlog=(d/'server.log').read_text(errors='replace')
                assert all(counts),(name,counts,serverlog)
                page=request()
                if name in ('timing-tie', 'timing-repeat'):
                    assert 'cache rejected: consensus decision; weighted loss not established for this arrival' in page, (name,page,serverlog)
                    assert 'ESCALATED to DISTRUST' not in serverlog, serverlog
                    if name == 'timing-repeat':
                        assert 'cache rejected: existing CW record marked error; original cause unknown' in page, (name,page)
                        assert 'السبب الأصلي غير معروف' in page, name
                else:
                    assert 'cache rejected: consensus weighted decision' in page, (name,page,serverlog)
                    assert 'ESCALATED to DISTRUST' in serverlog and 'consensus-mismatch' in serverlog, serverlog
                print(f'PASS {name}: replies={counts}, expected reputation behavior confirmed')
            finally:
                stop(server)
                for p in procs:stop(p)
