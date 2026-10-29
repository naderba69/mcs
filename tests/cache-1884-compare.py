"""Isolated CSP characterization; only synthetic keys and local child processes."""
import tempfile,subprocess,time,socket,os, pathlib, urllib.request, base64
ROOT=pathlib.Path(__file__).resolve().parent.parent
def port(t):
 s=socket.socket(socket.AF_INET,t);s.bind(('127.0.0.1',0));p=s.getsockname()[1];s.close();return p
def stop(p):
 if p.poll() is None:
  p.terminate()
  try:p.wait(timeout=3)
  except subprocess.TimeoutExpired:p.kill();p.wait()
with tempfile.TemporaryDirectory(prefix='mcs-csp-comp-') as d:
 d=pathlib.Path(d);peer=d/'peer';client=d/'client'
 subprocess.run(['cc','-O2','-std=gnu89','-o',str(peer),str(ROOT/'tests/cachepeer.c')],check=True)
 subprocess.run(['cc','-O2','-std=gnu89','-w','-o',str(client),str(ROOT/'tests/ncclient.c'),str(ROOT/'src/des.c'),str(ROOT/'src/msg-newcamd.c'),str(ROOT/'src/md5.c')],check=True)
 for name,line in [('bare-no','FILTER: NO\nFILTER TIME: 10\n'),('legacy-filter','FILTER: 5\nFILTER TIME: 150\n'),('explicit-off','CACHE FILTER: OFF\n'),('standard-off','CACHE PROTECTION: STANDARD\nCACHE FILTER: OFF\n')]:
  hp,np,cp,pp=port(socket.SOCK_STREAM),port(socket.SOCK_STREAM),port(socket.SOCK_DGRAM),port(socket.SOCK_DGRAM)
  cfg=d/'cfg';cfg.write_text(f'HTTP PORT: {hp}\nHTTP USER: test\nHTTP PASS: test\nTELNET PORT: 0\nCACHE PORT: {cp}\nCACHE PEER: 127.0.0.1:{pp} {{ csp=1 }}\n'+line+f'\n[cachetest]\nCAID: 1884\nPROVIDERS: 000000\nPORT: {np}\nDCW HALFNULLED: NO\nDCW CHECK: YES\nDCW MINTIME: 0\nDCWFILTER PATTERN: OFF\nENABLE CACHE: YES\nCACHE TIMEOUT: 500\nCACHE STATIC: NO\nSERVER TIMEOUT: 5000\nUSER: u1 p1\n')
  log=d/f'{name}-peer.log'; srvlog=d/f'{name}-server.log'
  with log.open('w+') as pf,srvlog.open('w+') as sf:
   env={k:v for k,v in os.environ.items() if not k.startswith('CP_')}
   peerp=subprocess.Popen(['stdbuf','-o0','-e0',str(peer),str(cp),str(pp),'11223366445566FF77889998AABBCC31','5'],stdout=pf,stderr=subprocess.STDOUT,cwd=d,env=env)
   srv=None
   try:
    time.sleep(.2);srv=subprocess.Popen(['stdbuf','-o0','-e0',str(ROOT/'bin/multics.x64'),'-C',str(cfg),'-v'],stdout=sf,stderr=subprocess.STDOUT,cwd=d)
    deadline=time.monotonic()+20
    while time.monotonic()<deadline:
     if peerp.poll() is not None or srv.poll() is not None:raise RuntimeError('child exited')
     if 'advertised card' in log.read_text(errors='replace'):break
     time.sleep(.1)
    else:raise RuntimeError('peer handshake failed')
    run=subprocess.run([str(client),'127.0.0.1',str(np),'u1','p1','0102030405060708091011121314','0','1884','1136'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
    peerout=log.read_text(errors='replace');serverout=srvlog.read_text(errors='replace')
    auth=base64.b64encode(b'test:test').decode()
    request=urllib.request.Request(f'http://127.0.0.1:{hp}/cache',headers={'Authorization':'Basic '+auth})
    with urllib.request.urlopen(request,timeout=5) as response: page=response.read().decode(errors='replace')
    want_filter='ON' if name in ('bare-no','legacy-filter') else 'OFF'
    assert 'Filter = '+want_filter in page,(name,'unexpected filter state')
    assert 'got request sid 1136' in peerout and 'PEER: replied CW' in peerout,(name,'peer did not receive and answer request')
    expected=2 if name=='explicit-off' else 0
    assert run.stdout.count('DELIVERED A CONTROL WORD') == expected,(name,run.stdout)
    if name=='standard-off': assert '[CONSENSUS] holding cw' in serverout,(name,'no consensus hold observed')
    print(name,'delivered=',run.stdout.count('DELIVERED A CONTROL WORD'),'peer_request=',peerout.count('request'), 'client_exit=',run.returncode, 'consensus=', serverout.count('CONSENSUS'))
    print('  PASS: filter',want_filter,'; expected synthetic delivery and peer request observed')
   finally:
    if srv:stop(srv)
    stop(peerp)
