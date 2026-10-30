#!/usr/bin/env python3
"""Three synthetic CSP peers, one Newcamd client, local processes only."""
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
    # A declared cycle is required by the current filter even if three peers
    # agree on the bytes. Agreement does not prove the cycle or independence.
    for name, mark, expected in [('unmarked',0,False),('marked',1,True)]:
        tcp=set()
        while len(tcp)<3:tcp.add(port(socket.SOCK_STREAM))
        np,hp,ccport=tcp; cp=port(socket.SOCK_DGRAM); peers=[]
        while len(peers)<3:
            p=port(socket.SOCK_DGRAM)
            if p!=cp and p not in peers:peers.append(p)
        cfg=d/'config'; cfg.write_text(f'HTTP PORT: {hp}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {ccport}\nF: traceuser tracepass\nCACHE PORT: {cp}\nCACHE PROTECTION: STANDARD\nCACHE FILTER: ON\n'+''.join(f'CACHE PEER: 127.0.0.1:{p} {{ csp=1; fwd=1 }}\n' for p in peers)+f'\n[test]\nCAID: 1884\nPROVIDERS: 000000\nPORT: {np}\nDCW CHECK: YES\nDCW HALFNULLED: NO\nDCW MINTIME: 0\nDCWFILTER PATTERN: OFF\nDCW TIMEOUT: 4000\nENABLE CACHE: YES\nCACHE STATIC: NO\nCACHE TIMEOUT: 900\nSERVER TIMEOUT: 4000\nUSER: u1 p1\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('CP_','NC_'))}
        env['CP_CYCLE_MARK']=str(mark); procs=[]; server=None
        with contextlib.ExitStack() as stack:
            logs=[stack.enter_context((d/f'peer{i}.log').open('w+')) for i in range(3)]
            slog=stack.enter_context((d/'server.log').open('w+'))
            try:
                for p,log in zip(peers,logs):
                    procs.append(subprocess.Popen(['stdbuf','-o0','-e0',str(peer),str(cp),str(p),'11223366445566FF77889998AABBCC31','5'],cwd=d,env=env,stdout=log,stderr=subprocess.STDOUT))
                server=subprocess.Popen(['stdbuf','-o0','-e0',str(BINARY),'-C',str(cfg),'-v'],cwd=d,stdout=slog,stderr=subprocess.STDOUT)
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    if server.poll() is not None or any(p.poll() is not None for p in procs):raise RuntimeError('child exited')
                    if all('advertised card' in (d/f'peer{i}.log').read_text(errors='replace') for i in range(3)):break
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
                delivered='THE SERVER DELIVERED A CONTROL WORD' in run.stdout
                counts=[(d/f'peer{i}.log').read_text(errors='replace').count('PEER: replied CW') for i in range(3)]
                serverlog=(d/'server.log').read_text(errors='replace')
                assert all(counts),(name,counts,serverlog)
                assert delivered == expected,(name,run.stdout,serverlog)
                assert '[CONSENSUS] holding cw' in serverlog,(name,serverlog)
                page=request()
                assert 'cache consensus vote: verdict=proceed; peer IDs=3; weight=3; threshold=3' in page,(name,page)
                assert 'تعددها لا يثبت استقلال المصادر' in page,name
                if name=='unmarked':
                    assert 'cache not forwarded: filter did not establish cycle' in page,name
                print(f'PASS {name}: replies={counts}, delivered={delivered}')
            finally:
                stop(server)
                for p in procs:stop(p)
