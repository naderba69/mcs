#!/usr/bin/env python3
"""Network regression: identical ECM payloads under different providers.

Use MODE=same or MODE=different. The old binary must JOIN in both cases;
fixed code joins only same-provider requests. No real picture is asserted.
"""
import base64, importlib.util, os, re, subprocess, sys, tempfile, time, urllib.parse, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
BINARY=Path(sys.argv[1]).resolve()
mode=os.environ.get('MODE','different')
assert mode in ('same','different')
first_provid=os.environ.get('FIRST_PROVID','000000')
assert first_provid in ('000000','000001')
timing=os.environ.get('TIMING','pending')
assert timing in ('pending','completed')
second_provid=first_provid if mode=='same' else ('000001' if first_provid=='000000' else '000000')
spec=importlib.util.spec_from_file_location('fixture',ROOT/'tests/cccam-trace-success.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
with tempfile.TemporaryDirectory(prefix='mcs-provider-identity-') as folder:
 tmp=Path(folder); cc=tmp/'ccdcw';nc=tmp/'ncserver'
 subprocess.run(['cc','-O2','-std=gnu89','-o',str(cc),str(ROOT/'tests/ccdcw.c')],check=True)
 subprocess.run(['cc','-O2','-std=gnu89','-w','-o',str(nc),str(ROOT/'tests/ncserver.c'),str(ROOT/'src/des.c'),str(ROOT/'src/msg-newcamd.c'),str(ROOT/'src/md5.c')],check=True)
 ports=set()
 while len(ports)<4:ports.add(f.free_port())
 http,cccam,newcamd,card=ports
 cfg=tmp/'mcs.cfg';cfg.write_text(f'HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {cccam}\nF: first password\nF: second password\nN: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n[isolated]\nCAID: 1884\nPROVIDERS: 000000, 000001\nPORT: {newcamd}\nDCW TIMEOUT: 5000\nSERVER TIMEOUT: 4500\nDCW HALFNULLED: YES\nDCW SWAP: YES\nUSER: first password\nUSER: second password\n')
 logs=[];children=[]
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
  watch('first',first_provid)
  first=launch([str(cc),'127.0.0.1',str(cccam),'first','password','1884','1136','0x80:7'],'first.log',dict(os.environ,CCDCW_PROVID=first_provid))
  f.wait_until(lambda:'ECM sent to server' in request(),children,10)
  if timing=='completed':
   assert first.wait(timeout=15)==0
   assert 'ECM1 tag=80 DCW' in (tmp/'first.log').read_text(errors='replace')
  watch('second',second_provid)
  second=launch([str(cc),'127.0.0.1',str(cccam),'second','password','1884','1136','0x80:7'],'second.log',dict(os.environ,CCDCW_PROVID=second_provid))
  def saw_second():
   page=request();return 'new ECM accepted' in page or 'joined existing ECM; earlier events unavailable' in page
  f.wait_until(saw_second,[peer,server],10);page=request()
  expected='joined existing ECM; earlier events unavailable' if mode=='same' else 'new ECM accepted'
  assert expected in page, ('expected '+expected, page)
  print('PASS: '+timing+' '+first_provid+' -> '+second_provid+': '+expected)
 except Exception:
  for log in logs:
   log.flush();log.seek(0);print('\n--- '+str(log.name)+' ---\n'+log.read()[-10000:],file=sys.stderr)
  raise
 finally:
  for p in reversed(children):f.stop(p)
  for log in logs:log.close()
