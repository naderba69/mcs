#!/usr/bin/env python3
"""Network regression: identical ECM payloads under different providers.

Use MODE=same or MODE=different. The old binary must JOIN in both cases;
fixed code joins only same-provider requests. No real picture is asserted.
"""
import base64, importlib.util, os, re, socket, subprocess, sys, tempfile, time, urllib.parse, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
BINARY=Path(sys.argv[1]).resolve()
mode=os.environ.get('MODE','different')
assert mode in ('same','different')
first_provid=os.environ.get('FIRST_PROVID','000000')
assert first_provid in ('000000','000001')
cache_collision=os.environ.get('CACHE_COLLISION') == '1'
inject=os.environ.get('CACHE_INJECT') == '1'
prefill=os.environ.get('CACHE_PREFILL') == '1'
if inject and (not cache_collision or mode != 'different'): raise ValueError('injection needs different-provider collision')
timing=os.environ.get('TIMING','pending')
if prefill and (not cache_collision or timing != 'completed' or mode != 'different'): raise ValueError('prefill requires completed different-provider case')
if cache_collision and timing != 'pending' and not prefill: raise ValueError('cache collision case requires pending ECMs')
assert timing in ('pending','completed')
second_provid=first_provid if mode=='same' else ('000001' if first_provid=='000000' else '000000')
spec=importlib.util.spec_from_file_location('fixture',ROOT/'tests/cccam-trace-success.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
with tempfile.TemporaryDirectory(prefix='mcs-provider-identity-') as folder:
 tmp=Path(folder); cc=tmp/'ccdcw';nc=tmp/'ncserver'
 subprocess.run(['cc','-O2','-std=gnu89','-o',str(cc),str(ROOT/'tests/ccdcw.c')],check=True)
 subprocess.run(['cc','-O2','-std=gnu89','-w','-o',str(nc),str(ROOT/'tests/ncserver.c'),str(ROOT/'src/des.c'),str(ROOT/'src/msg-newcamd.c'),str(ROOT/'src/md5.c')],check=True)
 ports=set()
 while len(ports)<6:ports.add(f.free_port())
 http,cccam,newcamd,card,cacheport,peerport=ports
 cacheconfig = f'CACHE PORT: {cacheport}\nCACHE PEER: 127.0.0.1:{peerport} {{ csp=1 }}\nCACHE FILTER: OFF\nCACHE THRESHOLD: 0\n' if cache_collision else ''
 cfg=tmp/'mcs.cfg';cfg.write_text(f'HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {cccam}\nF: first password\nF: second password\nN: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n{cacheconfig}\n[isolated]\nCAID: 1884\nPROVIDERS: 000000, 000001\nPORT: {newcamd}\nDCW TIMEOUT: 5000\nSERVER TIMEOUT: 4500\nDCW HALFNULLED: YES\nDCW SWAP: YES\n' + ('ENABLE CACHE: YES\nCACHE STATIC: NO\nCACHE TIMEOUT: 1000\n' if cache_collision else '') + 'USER: first password\nUSER: second password\n')
 logs=[];children=[]
 udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM) if (inject or prefill) else None
 if udp: udp.bind(('127.0.0.1',peerport))
 def launch(args,log,env=None):
  file=(tmp/log).open('w+');logs.append(file);p=subprocess.Popen(args,cwd=tmp,env=env,stdout=file,stderr=subprocess.STDOUT);children.append(p);return p
 try:
  env=dict(os.environ,NS_DELAYS='3000');
  for key in ('NS_HALF_CW','NS_BAD_CHECKSUM','NS_NEGATIVE','NS_NEGATIVE_FIRST','NS_OPPOSITE_HALF','NS_CW_SEQUENCE'):env.pop(key,None)
  peer=launch([str(nc),str(card),'0102030405060708091011121314','1884',first_provid],'peer.log',env);time.sleep(.3)
  server=launch(['stdbuf','-oL','-eL',str(BINARY),'-v','-C',str(cfg)],'server.log')
  f.wait_until(lambda:f.connected(cccam),children);f.wait_until(lambda:'login from user' in (tmp/'peer.log').read_text(errors='replace'),children)
  auth='Basic '+base64.b64encode(b'test:isolated').decode();url=f'http://127.0.0.1:{http}/user-trace'
  def request(data=None):
   with urllib.request.urlopen(urllib.request.Request(url,data=data,headers={'Authorization':auth}),timeout=5) as r:return r.read().decode()
  def watch(user,provid):
   p=request();token=re.search("name='csrf' value='([a-f0-9]{64})'",p);client=re.search("<option value='(\\d+)'[^>]*>"+user,p);assert token and client
   request(urllib.parse.urlencode({'csrf':token.group(1),'action':'start','client':client.group(1),'caid':'1884','provid':provid,'sid':'1136'}).encode())
  if udp:
   udp.settimeout(3)
   ping=bytearray(13);ping[0]=3;ping[11:13]=peerport.to_bytes(2,'big')
   udp.sendto(ping,('127.0.0.1',cacheport))
   udp.recvfrom(128)  # CSP ping reply confirms that the configured peer is recognized
  watch('first',first_provid)
  first=launch([str(cc),'127.0.0.1',str(cccam),'first','password','1884','1136','0x80:7'],'first.log',dict(os.environ,CCDCW_PROVID=first_provid,CCDCW_PRINT_CW='1'))
  f.wait_until(lambda:'ECM sent to server' in request(),children,10)
  if timing=='completed':
   assert first.wait(timeout=15)==0
   assert 'ECM1 tag=80 DCW' in (tmp/'first.log').read_text(errors='replace')
   if prefill:
    first_out=(tmp/'first.log').read_text(errors='replace')
    first_cw=re.search(r'CW=([0-9A-F]{32})',first_out).group(1)
    body=bytearray((i*7+1+0x36+7*0x40)&255 for i in range(3,64));body[0] ^= 7 >> 2
    hv=0
    for byte in body: hv=(31*hv+byte)&0xffffffff
    packet=bytes([2,0x80,0x11,0x36,0,0,0x18,0x84])+hv.to_bytes(4,'big')+bytes([0x80])+bytes.fromhex(first_cw)
    time.sleep(.2) # allow the local successful result to enter the cache
    for i in range(8):
     udp.sendto(packet,('127.0.0.1',cacheport));time.sleep(.08)
  watch('second',second_provid)
  second_start=time.monotonic()
  second=launch([str(cc),'127.0.0.1',str(cccam),'second','password','1884','1136','0x80:7'],'second.log',dict(os.environ,CCDCW_PROVID=second_provid,CCDCW_PRINT_CW='1'))
  def saw_second():
   page=request();return 'new ECM accepted' in page or 'joined existing ECM; earlier events unavailable' in page
  f.wait_until(saw_second,[peer,server],10);page=request()
  expected='joined existing ECM; earlier events unavailable' if mode=='same' else 'new ECM accepted'
  assert expected in page, ('expected '+expected, page)
  if cache_collision and not prefill:
   collision='cache provider collision: different local provider; conflicting operation skipped'
   if mode=='different':
    f.wait_until(lambda: collision in request(),[peer,server],5)
    page = request()
    assert 'تعارض قيم provider المحلية في سجل كاش' in page
    assert 'الحدث للقناة فقط ولا يثبت هوية الطلب أو provider' in page
    row = next(row for row in page.split('<tr>') if collision in row)
    assert 'قناة فقط، ليس ربط عميل' in row
    assert '<summary>إظهار CW</summary>' not in row
    summary = page.split('<h2>خلاصة آخر محاولة مسجلة</h2>',1)[1].split('<table>',1)[0]
    assert collision not in summary
   else: assert collision not in request()
  if prefill:
   assert second.wait(timeout=12)==0
   second_out=(tmp/'second.log').read_text(errors='replace')
   target=os.environ.get('CACHE_PREFILL_EXPECT','delivered')
   if target=='delivered':
    assert 'CW='+first_cw in second_out, (first_out,second_out,request())
    assert 'cache forwarded CW to ECM pipe; delivery not confirmed' in request()
   else:
    page=request()
    assert 'cache forwarded CW to ECM pipe; delivery not confirmed' not in page, page
    assert 'cache provider collision: different local provider; conflicting operation skipped' in page
    assert 'تعارض قيم provider المحلية في سجل كاش' in page
    if 'ECM1 tag=80 DCW' in second_out:
     assert 'Newcamd CW response; validity not determined' in page, page
     assert time.monotonic()-second_start >= 2, second_out
   print('PASS: after first completion, cached CW reuse '+target+' for different numeric provider; a source may return identical bytes')
  if inject:
   body=bytearray((i*7+1+0x36+7*0x40)&255 for i in range(3,64))
   body[0] ^= 7 >> 2
   hash_value=0
   for byte in body: hash_value=(31*hash_value+byte)&0xffffffff
   packet=bytes([2,0x80,0x11,0x36,0,0,0x18,0x84])+hash_value.to_bytes(4,'big')+bytes([0x80])+bytes.fromhex('11223366445566FF77889998AABBCC31')
   udp.sendto(packet,('127.0.0.1',cacheport))
   assert second.wait(timeout=10)==0
   assert first.wait(timeout=10)==0
   second_output=(tmp/'second.log').read_text(errors='replace')
   first_output=(tmp/'first.log').read_text(errors='replace')
   expected=os.environ.get('CACHE_INJECT_TARGET','first')
   assert ('11223366445566FF77889998AABBCC31' in first_output) == (expected=='first'), (first_output,second_output)
   assert ('11223366445566FF77889998AABBCC31' in second_output) == (expected=='second'), (first_output,second_output)
   print('PASS: synthetic CSP CW delivered to '+expected+' requester; provider in CSP is absent')
  print('PASS: '+timing+' '+first_provid+' -> '+second_provid+': '+expected)
 except Exception:
  for log in logs:
   log.flush();log.seek(0);print('\n--- '+str(log.name)+' ---\n'+log.read()[-10000:],file=sys.stderr)
  raise
 finally:
  for p in reversed(children):f.stop(p)
  for log in logs:log.close()
  if udp: udp.close()
