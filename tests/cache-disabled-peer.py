#!/usr/bin/env python3
"""CSP egress: a persisted banned peer must not receive an accepted local CW."""
#!/usr/bin/env python3
"""CSP automatic push: online peers, real UDP, synthetic CWs only.

A local server answer is not a cache-peer vote; its broadcast behavior is
characterized separately from the cache ingress threshold. No picture asserted.
"""
import base64, re, socket, subprocess, tempfile, time, sys, urllib.request, urllib.parse
from pathlib import Path
import os
ROOT=Path(__file__).resolve().parents[1]
binary=Path(sys.argv[1]).resolve(); mode=sys.argv[2]
def port(kind):
 s=socket.socket(socket.AF_INET,kind);s.bind(('127.0.0.1',0)); p=s.getsockname()[1];s.close();return p
with tempfile.TemporaryDirectory(prefix='mcs-peer-serve-') as d:
 d=Path(d); local=True; card=port(socket.SOCK_STREAM)
 if local:
  subprocess.run(['cc','-O2','-std=gnu89','-o',str(d/'ccdcw'),str(ROOT/'tests/ccdcw.c')],check=True)
  subprocess.run(['cc','-O2','-std=gnu89','-w','-o',str(d/'ncserver'),str(ROOT/'tests/ncserver.c'),str(ROOT/'src/des.c'),str(ROOT/'src/msg-newcamd.c'),str(ROOT/'src/md5.c')],check=True)
 cp=port(socket.SOCK_DGRAM); hp=port(socket.SOCK_STREAM); cc=port(socket.SOCK_STREAM); nc=port(socket.SOCK_STREAM)
 peers=[]
 for _ in range(2):
  s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('127.0.0.1',0));s.settimeout(.15);peers.append(s)
 pp=[s.getsockname()[1] for s in peers]
 rep=d/'reputation';rep.write_text(f'127.0.0.1:{pp[1]} 3 60 seeded\n')
 cfg=d/'mcs.cfg';cfg.write_text(f'''HTTP PORT: {hp}
HTTP USER: test
HTTP PASS: isolated
TELNET PORT: 0
CCCAM PORT: {cc}
F: traceuser tracepass
{f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14' if local else ''}
PEER REPUTATION: ON
PEER REPUTATION FILE: {rep}
CACHE PORT: {cp}
CACHE FORWARD: {'NO' if mode=='local-forward-off' else 'YES'}
CACHE FILTER: {'ON' if mode=='filter' else 'OFF'}
CACHE THRESHOLD: {5 if mode in ('threshold','threshold-pass','local-threshold') else 1}
CACHE CONSENSUS: {'ON' if mode=='held' else 'OFF'}
CACHE CONSENSUS WINDOW: 2000
CACHE CONSENSUS SOURCES: 3
CACHE PEER: 127.0.0.1:{pp[0]} {{ csp=1 }}
CACHE PEER: 127.0.0.1:{pp[1]} {{ csp=1; fwd=1 }}
[test]
CAID: 1884
PROVIDERS: 000000
PORT: {nc}
ENABLE CACHE: YES
CACHE STATIC: NO
{('CACHE SENDREP: NO' if mode=='local-sendrep-off' else '')}
CACHE TIMEOUT: 1000
USER: u1 p1
''')
 source_log=(d/'source.log').open('w+');source=None
 if local:source=subprocess.Popen([str(d/'ncserver'),str(card),'0102030405060708091011121314','1884','000000'],cwd=d,stdout=source_log,stderr=subprocess.STDOUT)
 log=(d/'server.log').open('w+');p=subprocess.Popen(['stdbuf','-oL','-eL',str(binary),'-v','-C',str(cfg)],cwd=d,stdout=log,stderr=subprocess.STDOUT)
 try:
  url=f'http://127.0.0.1:{hp}/user-trace'; auth={'Authorization':'Basic '+base64.b64encode(b'test:isolated').decode()}
  def request(data=None):
   with urllib.request.urlopen(urllib.request.Request(url,data=data,headers=auth),timeout=2) as r:return r.read().decode()
  until=time.monotonic()+12
  while True:
   if p.poll() is not None: raise RuntimeError('server exited')
   try: page=request(); break
   except OSError:
    if time.monotonic()>until:raise
    time.sleep(.05)
  token=re.search("name='csrf' value='([a-f0-9]{64})'",page).group(1);cid=re.search("<option value='(\\d+)'[^>]*>traceuser",page).group(1)
  request(urllib.parse.urlencode({'csrf':token,'action':'start','client':cid,'caid':'1884','provid':'000000','sid':'1136'}).encode())
  # Answer the server's own pings: ping>0 and card presence are required
  # to receive an unsolicited forward.
  import select
  online=set(); deadline=time.monotonic()+14
  while len(online)<2:
   if time.monotonic()>deadline: raise AssertionError(('peer handshake timeout',online,(d/'server.log').read_text()))
   ready,_,_=select.select(peers,[],[],.2)
   for sock in ready:
    pkt,_=sock.recvfrom(1024)
    if len(pkt)>=9 and pkt[0]==3:
     ack=bytearray(16);ack[0]=0x10 if pkt[1:3]==b'MC' else 4;ack[4:6]=pkt[4:6];sock.sendto(ack,('127.0.0.1',cp))
     sock.sendto(bytes.fromhex('130118840000'),('127.0.0.1',cp));online.add(sock.fileno())
  deadline=time.monotonic()+3
  while 'ban restored from the reputation file' not in (d/'server.log').read_text(errors='replace'):
   if time.monotonic()>deadline: raise AssertionError('ban not restored')
   time.sleep(.03)
  time.sleep(.3)
  key=bytes([0x80,0x11,0x36,0,0,0x18,0x84,0x12,0x34,0x56,0x78]);cw=bytes.fromhex('11223366445566ff77889998aabbcc31')
  if local:
   deadline=time.monotonic()+10
   while 'login from user' not in (d/'source.log').read_text(errors='replace'):
    if time.monotonic()>deadline: raise AssertionError('source not connected')
    time.sleep(.05)
   out=subprocess.run([str(d/'ccdcw'),'127.0.0.1',str(cc),'traceuser','tracepass','1884','1136','0x80:7'],capture_output=True,text=True,timeout=12,env=dict(os.environ,CCDCW_PROVID='000000',CCDCW_PRINT_CW='1'),check=True).stdout
   cw=bytes.fromhex(re.search(r'CW=([0-9A-F]{32})',out).group(1));key=bytes.fromhex('80113600001884a0dd9a9f');time.sleep(.4)
  else: peers[0].sendto(bytes([2])+key+bytes([0x80])+cw,('127.0.0.1',cp))
  expect={'local':'CW response sent; picture not verified','local-forward-off':'CW response sent; picture not verified','local-sendrep-off':'CW response sent; picture not verified','local-threshold':'CW response sent; picture not verified','off':'cache CW entered validation; decision pending','held':'cache held: consensus awaiting corroboration','filter':'cache not forwarded: filter did not establish cycle','threshold':'cache held: threshold not reached','threshold-pass':'cache CW entered validation; decision pending'}[mode]
  deadline=time.monotonic()+2
  while expect not in (page:=request()):
   if time.monotonic()>deadline: raise AssertionError(('not observed',expect,page))
   time.sleep(.02)
  if mode=='threshold-pass':
   for _ in range(4):peers[0].sendto(bytes([2])+key+bytes([0x80])+cw,('127.0.0.1',cp));time.sleep(.05)
  # Observe both peers: only the normal one may receive the local CW.
  deadline=time.monotonic()+1; got={0:[],1:[]}
  while time.monotonic()<deadline:
   ready,_,_=select.select(peers,[],[],.1)
   for sock in ready:
    data,_=sock.recvfrom(1024)
    if len(data)>=29 and data[0]==2 and data[1:12]==key and data[13:29]==cw:
     got[0 if sock is peers[0] else 1].append(data)
  assert got[0], 'control: enabled peer did not receive the local CW'
  assert not got[1], 'banned peer received the local CW'
  print('PASS: enabled peer received CW; persisted banned peer received none')

 finally:
  p.terminate();
  try:p.wait(timeout=3)
  except subprocess.TimeoutExpired:p.kill();p.wait()
  log.close()
  if source:
   source.terminate()
   try:source.wait(timeout=3)
   except subprocess.TimeoutExpired:source.kill();source.wait()
  source_log.close()
  for s in peers:s.close()
