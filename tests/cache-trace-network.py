#!/usr/bin/env python3
"""Isolated CSP + CCcam trace, no production services or credentials."""
import base64
import os
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
    for name, policy, verdict, static in [
        ('checksum', 'CACHE FILTER: OFF\n', 'cache rejected: profile CW checksum validation failed', 'NO'),
        ('checksum-excluded', 'CACHE FILTER: OFF\n', 'cache forwarded CW to ECM pipe; delivery not confirmed', 'NO'),
        ('checksum-enforced', 'CACHE FILTER: OFF\n', 'cache rejected: profile CW checksum validation failed', 'NO'),
        ('short8', 'CACHE FILTER: OFF\n', 'cache forwarded CW to ECM pipe; delivery not confirmed', 'NO'),
        ('short12', 'CACHE FILTER: OFF\n', 'CSP reply dropped: incomplete integrity header; length=12; header unverified', 'NO'),
        ('short20', 'CACHE FILTER: OFF\n', 'CSP reply dropped: incomplete CW payload; length=20; header unverified', 'NO'),
        ('oversize', 'CACHE FILTER: OFF\n', 'CSP reply dropped: oversized datagram; length=513; header unverified', 'NO'),
        ('integrity', 'CACHE FILTER: OFF\n', 'CSP reply dropped: integrity tag mismatch; length=29; header unverified', 'NO'),
        ('tag', 'CACHE FILTER: OFF\n', 'CSP reply dropped: invalid ECM tag; length=29; header unverified', 'NO'),
        ('hash', 'CACHE FILTER: OFF\n', 'CSP reply dropped: missing channel or hash; length=29; header unverified', 'NO'),
        ('unknown', 'CACHE FILTER: OFF\n', 'CSP reply dropped: unknown sender; length=29; header unverified', 'NO'),
        ('filter', 'CACHE FILTER: ON\n', 'cache not forwarded: filter did not establish cycle', 'NO'),
        ('capacity', 'CACHE PROTECTION: STANDARD\nCACHE FILTER: OFF\n', 'cache consensus vote: no candidate slot; verdict=proceed; agreement unconfirmed', 'NO'),
        ('consensus', 'CACHE PROTECTION: STANDARD\nCACHE FILTER: OFF\n', 'cache held: consensus awaiting corroboration', 'NO'),
        ('accepted', 'CACHE FILTER: OFF\n', 'cache forwarded CW to ECM pipe; delivery not confirmed', 'NO'),
        ('static', 'CACHE FILTER: ON\n', 'cache not forwarded: filter did not establish cycle', 'YES'),
    ]:
        if os.environ.get('MCS_CACHE_TRACE_CASE') and name != os.environ['MCS_CACHE_TRACE_CASE']:
            continue
        hp, ccport, ncport = [port(socket.SOCK_STREAM) for _ in range(3)]
        cp, pp = [port(socket.SOCK_DGRAM) for _ in range(2)]
        cfg = tmp/'config'
        profile_policy = ('DCWFILTER CHECKSUM: OFF\nENABLE SKIPCWC: YES\nSKIPCWC_EXCLUDE_SIDS_ACTIVE: YES\nSKIPCWC_EXCLUDE_SIDLIST: ' + ('1136' if name == 'checksum-excluded' else '9999') + '\n') if name in ('checksum-excluded', 'checksum-enforced') else ''
        cfg.write_text(f'HTTP PORT: {hp}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {ccport}\nF: traceuser tracepass\nCACHE PORT: {cp}\nCACHE PEER: 127.0.0.1:{pp} {{ csp=1 }}\n'+policy+f'\n[test]\nCAID: 1884\nPROVIDERS: 000000\nPORT: {ncport}\nDCW HALFNULLED: NO\nDCW CHECK: YES\nDCW MINTIME: 0\nDCWFILTER PATTERN: OFF\n{profile_policy}ENABLE CACHE: YES\nCACHE STATIC: {static}\nCACHE TIMEOUT: 900\nSERVER TIMEOUT: 5000\nUSER: traceuser tracepass\n')
        with (tmp/'peer.log').open('w+') as plog, (tmp/'server.log').open('w+') as slog:
            peer_cw = '11223367445566FF77889998AABBCC31' if name in ('checksum', 'checksum-excluded', 'checksum-enforced') else '11223366445566FF77889998AABBCC31'
            peer_env = {k:v for k,v in os.environ.items() if not k.startswith('CP_')}
            if name == 'capacity': peer_env['CP_TEST_CAPACITY_FILL'] = '1'
            if name in ('short8', 'short12', 'short20', 'oversize', 'integrity', 'tag', 'hash', 'unknown'): peer_env['CP_TEST_DROP'] = name
            peerproc = subprocess.Popen(['stdbuf','-o0','-e0',str(peer),str(cp),str(pp),peer_cw,'5'], cwd=tmp, env=peer_env, stdout=plog, stderr=subprocess.STDOUT)
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
                if name in ('checksum-excluded', 'checksum-enforced'):
                    assert 'cache CW validation policy: global; no attached ECM profile' not in page, name
                assert 'خلاصة آخر محاولة مسجلة' in page, name
                assert f'cache wait policy: initial schedule=900 ms; static={static}' in page, (name, re.findall(r'cache wait policy[^<]*', page))
                summary = page.split('<h2>خلاصة آخر محاولة مسجلة</h2>', 1)[1].split('<table>', 1)[0]
                assert 'سياسة انتظار الكاش عند البحث' in summary, (name, summary)
                assert 'أحداث الكاش المصنفة' in summary, name
                assert verdict not in summary, (name, 'channel verdict attributed to client')
                if name == 'static':
                    assert 'cache wait policy: ECM thread timeout=500 ms; profile=900 ms; static=YES' in page, name
                assert 'قناة فقط، ليس ربط عميل' in page, name
                assert peer_cw in page, name
                if name == 'short8':
                    assert 'ردود CSP قصيرة جدًا لا تكفي لقراءة القناة: 1' in page, page
                    assert 'CSP reply dropped:' not in page, name
                if name in ('short12', 'short20', 'oversize', 'integrity', 'tag', 'hash', 'unknown'):
                    assert 'ردود CSP قصيرة جدًا لا تكفي لقراءة القناة: 0' in page, name
                    assert 'cache forwarded CW to ECM pipe; delivery not confirmed' in page, name
                    assert 'header unverified' in page, name
                    arabic = {'short12':'ترويسة السلامة ناقصة', 'short20':'لا يحتوي CW كاملة',
                              'oversize':'لتجاوز حجم الرسالة المسموح', 'integrity':'لاختلاف بايت سلامة الترويسة',
                              'tag':'نوع ECM غير صالح', 'hash':'قناة أو بصمة ناقصة', 'unknown':'من عنوان غير معروف'}
                    row = next(row for row in page.split('<tr>') if verdict in row)
                    assert arabic[name] in row and 'قناة فقط، ليس ربط عميل' in row, (name,row)
                    assert 'data-event=' not in row, (name, 'unvalidated CW exposed')
                if name == 'capacity':
                    assert 'cache consensus capacity: used=64/64; held=64; overdue=0; reclaimable=0;' in page, page
                    assert 'سعة جدول Consensus العامة عند عدم توفر خانة' in page, name
                    assert 'كيف أقرأ أدلة الكاش؟' in page, name
                if name == 'checksum':
                    assert 'cache CW validation policy: attached ECM profile' in page, page
                    assert 'رفض الكاش CW: فشل تحقق checksum' in page, name
                    assert 'ECM1 tag=80 DCW' not in result.stdout, result.stdout
                if name in ('accepted', 'checksum-excluded'):
                    assert 'ECM1 tag=80 DCW' in result.stdout, result.stdout
                if name == 'checksum-enforced':
                    assert 'ECM1 tag=80 DCW' not in result.stdout, result.stdout
                    assert 'cache CW validation policy: attached ECM profile' in page, page
                print('PASS:', name, verdict)
            finally:
                stop(server); stop(peerproc)
