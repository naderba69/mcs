#!/usr/bin/env python3
"""Synthetic Newcamd CW sequence exercises a DCW CHECK rejection.

CCcam client -> production MultiCS binary -> isolated Newcamd card emulator.
Checks exact delivered CW bytes and the authenticated monitor; no real service,
receiver, or decrypted video is involved. Only owned child processes are stopped.
"""
import base64
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT/'src/x64/multics'
spec = importlib.util.spec_from_file_location('fixture', ROOT/'tests/cccam-trace-success.py')
f = importlib.util.module_from_spec(spec); spec.loader.exec_module(f)

def make_word(seed):
    raw = []
    for q in range(4):
        a = (seed + q * 31) & 255
        b = (seed * 3 + q * 17 + 9) & 255
        c = (seed * 7 + q * 11 + 3) & 255
        raw.extend((a, b, c, (a+b+c) & 255))
    return bytes(raw).hex().upper()

caid, sid = '09BD', '014A'
first = make_word(41)
second = '0'*16 + make_word(72)[16:]
words = [first, second]
tags = ['0x80:1', '0x81:2']
with tempfile.TemporaryDirectory(prefix='mcs-cw-sequence-') as folder:
    tmp = Path(folder); cc = tmp/'ccdcw'; nc = tmp/'ncserver'
    compiler = os.environ.get('CC', 'cc')
    subprocess.run([compiler, '-O2', '-std=gnu89', '-o', str(cc), str(ROOT/'tests/ccdcw.c')], check=True)
    subprocess.run([compiler, '-O2', '-std=gnu89', '-w', '-o', str(nc), str(ROOT/'tests/ncserver.c'), str(ROOT/'src/des.c'), str(ROOT/'src/msg-newcamd.c'), str(ROOT/'src/md5.c')], check=True)
    ports = set()
    while len(ports) < 4: ports.add(f.free_port())
    http, cccam, newcamd, card = ports
    cfg = tmp/'isolated.cfg'
    cfg.write_text(f'HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {cccam}\nF: traceuser tracepass\nN: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n[isolated]\nCAID: {caid}\nPROVIDERS: 0\nPORT: {newcamd}\nDCW HALFNULLED: NO\nDCW CHECK: YES\nDCW SWAP: YES\nDCW TIMEOUT: 3000\nUSER: traceuser tracepass\n')
    children = []
    with (tmp/'peer.log').open('w+') as peerlog, (tmp/'server.log').open('w+') as serverlog:
        try:
            env = dict(os.environ, NS_DELAYS='60', NS_VERBOSE='1', NS_CW_SEQUENCE=','.join(words))
            for key in ('NS_HALF_CW','NS_BAD_CHECKSUM','NS_NEGATIVE','NS_NEGATIVE_FIRST','NS_OPPOSITE_HALF'): env.pop(key, None)
            peer = subprocess.Popen([str(nc), str(card), '0102030405060708091011121314', caid, '000000'], cwd=tmp, env=env, stdout=peerlog, stderr=subprocess.STDOUT); children.append(peer)
            time.sleep(.3)
            server = subprocess.Popen([str(BINARY), '-v', '-C', str(cfg)], cwd=tmp, stdout=serverlog, stderr=subprocess.STDOUT); children.append(server)
            f.wait_until(lambda: f.connected(cccam), children)
            def ready():
                peerlog.flush()
                return 'login from user' in (tmp/'peer.log').read_text(errors='replace')
            f.wait_until(ready, children)
            auth = 'Basic '+base64.b64encode(b'test:isolated').decode()
            def request(data=None):
                req = urllib.request.Request(f'http://127.0.0.1:{http}/user-trace', data=data, headers={'Authorization':auth})
                with urllib.request.urlopen(req, timeout=5) as resp: return resp.read().decode()
            page = request()
            token = re.search("name='csrf' value='([a-f0-9]{64})'", page)
            user = re.search("<option value='(\\d+)'[^>]*>traceuser", page)
            assert token and user
            request(urllib.parse.urlencode({'csrf':token.group(1), 'action':'start', 'client':user.group(1), 'mode':'all'}).encode())
            result = subprocess.run([str(cc), '127.0.0.1', str(cccam), 'traceuser', 'tracepass', caid, sid, *tags], capture_output=True, text=True, timeout=40, check=True, env=dict(os.environ, CCDCW_PRINT_CW='1'))
            assert 'ECM1 tag=80 DCW' in result.stdout, result.stdout
            assert 'ECM2 tag=81 DCW' not in result.stdout, result.stdout
            page = request()
            message = 'CW rejected by DCW CHECK: zero half with previous decode'
            rows = re.findall(r'<tr>.*?</tr>', page, re.DOTALL)
            rejected = [row for row in rows if message in row]
            if os.environ.get('MCS_EXPECT_NO_REJECTION_TRACE') == '1':
                assert not rejected, rejected
            else:
                assert rejected, (result.stdout,page)
                assert all('ECM مشترك' in row and 'رفض DCW CHECK' in row for row in rejected), rejected
                assert all(re.match(r'<tr><td>[^<]*</td><td>2</td>', row) for row in rejected), rejected
            assert any(second in row and 'Newcamd CW response;' in row for row in rows), page
            print('PASS: DCW CHECK refusal and queued trace identity behavior verified')
        finally:
            for child in reversed(children): f.stop(child)
