#!/usr/bin/env python3
"""Pre-seeded CSP CW: repeated pushes from one peer must not bypass HOLD.

Local processes and synthetic keys only. Receipt of a CW does not prove picture.
"""
import base64, os, re, socket, subprocess, sys, tempfile, time, urllib.parse, urllib.request
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
def port(kind):
    with socket.socket(socket.AF_INET,kind) as s:
        s.bind(('127.0.0.1',0)); return s.getsockname()[1]
def stop(p):
    if p and p.poll() is None:
        p.terminate()
        try: p.wait(timeout=4)
        except subprocess.TimeoutExpired: p.kill(); p.wait(timeout=4)
with tempfile.TemporaryDirectory(prefix='mcs-consensus-prefill-') as folder:
    d=Path(folder); peer=d/'peer'; client=d/'client'
    subprocess.run(['cc','-O2','-std=gnu89','-o',str(peer),str(ROOT/'tests/cachepeer.c')],check=True)
    subprocess.run(['cc','-O2','-std=gnu89','-o',str(client),str(ROOT/'tests/ccdcw.c')],check=True)
    for name, armed in [('off',False),('held',True),('filter-on',False),('threshold',False),('threshold-pass',False)]:
        if os.environ.get('MCS_PREFILL_CASE') and name != os.environ['MCS_PREFILL_CASE']: continue
        cp,pp=port(socket.SOCK_DGRAM),port(socket.SOCK_DGRAM)
        hp,cc,np=port(socket.SOCK_STREAM),port(socket.SOCK_STREAM),port(socket.SOCK_STREAM)
        cfg=d/'config'; cfg.write_text(f'HTTP PORT: {hp}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {cc}\nF: traceuser tracepass\nCACHE PORT: {cp}\nCACHE FILTER: {"ON" if name == "filter-on" else "OFF"}\nCACHE THRESHOLD: {10 if name in ("threshold", "threshold-pass") else 1}\nCACHE CONSENSUS: {"ON" if armed else "OFF"}\nCACHE CONSENSUS WINDOW: 2000\nCACHE CONSENSUS SOURCES: 3\nCACHE PEER: 127.0.0.1:{pp} {{ csp=1; fwd=1 }}\n\n[test]\nCAID: 1884\nPROVIDERS: 000000\nPORT: {np}\nDCW TIMEOUT: 5000\nDCW CHECK: YES\nDCW HALFNULLED: NO\nENABLE CACHE: YES\nCACHE STATIC: NO\nCACHE TIMEOUT: 2000\nUSER: traceuser tracepass\n')
        env={k:v for k,v in os.environ.items() if not k.startswith('CP_')}
        env.update(CP_PUSH_HASH='3CDAC95E',CP_PUSH_SID='1136',CP_PUSH_CAID='1884',CP_REPUSH_MS='70',CP_REPUSH_SAME='1')
        with (d/'peer.log').open('w+') as plog, (d/'server.log').open('w+') as slog:
            p=subprocess.Popen(['stdbuf','-o0','-e0',str(peer),str(cp),str(pp),'11223366445566FF77889998AABBCC31','10'],cwd=d,env=env,stdout=plog,stderr=subprocess.STDOUT)
            server=None
            try:
                server=subprocess.Popen(['stdbuf','-o0','-e0',str(BINARY),'-C',str(cfg),'-v'],cwd=d,stdout=slog,stderr=subprocess.STDOUT)
                deadline=time.monotonic()+15
                online_count = None
                while time.monotonic()<deadline:
                    if p.poll() is not None or server.poll() is not None: raise RuntimeError('child exited')
                    peerlog=(d/'peer.log').read_text(errors='replace')
                    serverlog=(d/'server.log').read_text(errors='replace')
                    if 'come Online' in serverlog and online_count is None:
                        online_count = peerlog.count('unsolicited push #')
                    enough = serverlog.count('[CONSENSUS] holding cw') >= 6 if armed else (online_count is not None and peerlog.count('unsolicited push #') >= online_count + (12 if name == 'threshold-pass' else 6))
                    if enough and 'advertised card' in peerlog: break
                    time.sleep(.02)
                else: raise RuntimeError('prefill failed: '+peerlog)
                if name in ('threshold', 'threshold-pass'): stop(p)
                url=f'http://127.0.0.1:{hp}/user-trace'
                auth={'Authorization':'Basic '+base64.b64encode(b'test:isolated').decode()}
                def request(data=None):
                    with urllib.request.urlopen(urllib.request.Request(url,data=data,headers=auth),timeout=4) as response:
                        return response.read().decode()
                page=request()
                csrf=re.search("name='csrf' value='([a-f0-9]{64})'",page).group(1)
                uid=re.search("<option value='(\\d+)'[^>]*>traceuser",page).group(1)
                request(urllib.parse.urlencode({'csrf':csrf,'action':'start','client':uid,'caid':'1884','provid':'000000','sid':'1136'}).encode())
                started=time.monotonic()
                run=subprocess.run([str(client),'127.0.0.1',str(cc),'traceuser','tracepass','1884','1136','0x80'],capture_output=True,text=True,timeout=12,check=True)
                elapsed=time.monotonic()-started
                logs=(d/'server.log').read_text(errors='replace')
                if name in ('filter-on', 'threshold'):
                    assert 'ECM1 tag=80 DCW' not in run.stdout,(name,run.stdout,logs[-5000:])
                    page=request()
                    msg = 'cache find deferred: cycle not established' if name == 'filter-on' else 'cache find deferred: threshold not reached'
                    assert msg in page, (name, page)
                    assert 'قناة فقط، ليس ربط عميل' in page, page
                else:
                    assert 'ECM1 tag=80 DCW' in run.stdout,(name,run.stdout,logs[-5000:])
                if armed:
                    assert '[CONSENSUS] holding cw' in logs,(name,logs)
                    assert elapsed >= 0.7,(name,elapsed,'held CW delivered prematurely',run.stdout,logs[-5000:])
                    page=request()
                    assert 'cache find deferred: candidate held by consensus' in page, page
                    assert 'تأجل جلب CW المخزنة' in page, page
                    assert 'قناة فقط، ليس ربط عميل' in page, page
                elif name in ('off', 'threshold-pass'):
                    assert elapsed < 1,(name,elapsed,run.stdout)
                print(f'PASS {name}: {elapsed:.3f}s; same peer prefilled >=6')
            finally: stop(server); stop(p)
