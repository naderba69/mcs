#!/usr/bin/env python3
"""Isolated CSP/Newcamd consensus deadline and post-expiry cycle checks.

Synthetic keys only; children and random ports are owned by this test. A CW
reply does not demonstrate a TV picture. Pass a MultiCS binary as argv[1].
"""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

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
        except subprocess.TimeoutExpired:
            p.kill(); p.wait(timeout=5)

with tempfile.TemporaryDirectory(prefix='mcs-consensus-expiry-') as folder:
    d = Path(folder)
    peer, client = d/'peer', d/'client'
    subprocess.run(['cc','-O2','-std=gnu89','-o',str(peer),str(ROOT/'tests/cachepeer.c')], check=True)
    subprocess.run(['cc','-O2','-std=gnu89','-w','-o',str(client),str(ROOT/'tests/ncclient.c'),str(ROOT/'src/des.c'),str(ROOT/'src/msg-newcamd.c'),str(ROOT/'src/md5.c')], check=True)
    for name, filter_on, mark, expected in [('unmarked-filter-off',False,0,True),('unmarked-filter-on',True,0,False),('marked-filter-on',True,1,True),('marked-wrong-cycle',True,2,False)]:
        np, cp, pp = port(socket.SOCK_STREAM), port(socket.SOCK_DGRAM), port(socket.SOCK_DGRAM)
        conf=d/'config'
        cycle_rule = 'SID LIST: 1136.81\n' if name == 'marked-wrong-cycle' else ''
        conf.write_text(f'HTTP PORT: 0\nTELNET PORT: 0\nCACHE PORT: {cp}\nCACHE PROTECTION: STANDARD\nCACHE FILTER: {"YES" if filter_on else "NO"}\nCACHE PEER: 127.0.0.1:{pp} {{ csp=1; fwd=1 }}\n\n[test]\nCAID: 1884\nPROVIDERS: 000000\nPORT: {np}\n{cycle_rule}DCW CHECK: YES\nDCW HALFNULLED: NO\nDCW MINTIME: 0\nDCWFILTER PATTERN: OFF\nDCW TIMEOUT: 6000\nENABLE CACHE: YES\nCACHE STATIC: NO\nCACHE TIMEOUT: 900\nSERVER TIMEOUT: 5000\nUSER: u1 p1\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('CP_','NC_'))}
        env['CP_CYCLE_MARK']=str(mark)
        with (d/'peer.log').open('w+') as plog, (d/'server.log').open('w+') as slog:
            pp1=subprocess.Popen(['stdbuf','-o0','-e0',str(peer),str(cp),str(pp),'11223366445566FF77889998AABBCC31','5'],cwd=d,env=env,stdout=plog,stderr=subprocess.STDOUT)
            server=None
            try:
                server=subprocess.Popen(['stdbuf','-o0','-e0',str(BINARY),'-C',str(conf),'-v'],cwd=d,stdout=slog,stderr=subprocess.STDOUT)
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    if pp1.poll() is not None or server.poll() is not None: raise RuntimeError('isolated process exited')
                    if 'advertised card' in (d/'peer.log').read_text(errors='replace'): break
                    time.sleep(.1)
                else: raise RuntimeError('isolated peer not ready')
                client_env=dict(env,NC_ECMS='1')
                start=time.monotonic()
                run=subprocess.run([str(client),'127.0.0.1',str(np),'u1','p1','0102030405060708091011121314','0','1884','1136'],env=client_env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
                elapsed=time.monotonic()-start
                peerout=(d/'peer.log').read_text(errors='replace'); logs=(d/'server.log').read_text(errors='replace')
                assert 'got request sid 1136' in peerout and 'PEER: replied CW' in peerout, (name,peerout)
                assert '[CONSENSUS] holding cw' in logs, (name,logs)
                delivered='THE SERVER DELIVERED A CONTROL WORD' in run.stdout
                assert delivered == expected, (name,elapsed,run.stdout,logs)
                if expected:
                    assert .50 <= elapsed < 2.5, (name,elapsed,logs)
                print(f'PASS: {name} delivered={delivered}, elapsed={elapsed:.3f}s')
            finally:
                stop(server); stop(pp1)
