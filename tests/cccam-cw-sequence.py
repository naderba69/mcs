#!/usr/bin/env python3
"""End-to-end synthetic ECM and changing/repeated CW sequence.

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

half = os.environ.get('MCS_SEQ_HALF') == '1'
caid, sid = ('09BD', '014A') if half else ('1884', '1136')
words = [make_word(41 + i * 7) for i in range(16)]
if half:
    words = [(word[:16] + '0'*16) if i % 2 == 0 else ('0'*16 + word[16:])
             for i, word in enumerate(words)]
    words[6] = words[4]  # same cycle, distinct ECM
else:
    words[5] = words[4]  # valid, repeated CW for a distinct ECM
assert len(set(words)) == 15
tags = [f"0x{0x80 + i % 2:02x}:{i+1}" for i in range(len(words))]
with tempfile.TemporaryDirectory(prefix='mcs-cw-sequence-') as folder:
    tmp = Path(folder); cc = tmp/'ccdcw'; nc = tmp/'ncserver'
    compiler = os.environ.get('CC', 'cc')
    subprocess.run([compiler, '-O2', '-std=gnu89', '-o', str(cc), str(ROOT/'tests/ccdcw.c')], check=True)
    subprocess.run([compiler, '-O2', '-std=gnu89', '-w', '-o', str(nc), str(ROOT/'tests/ncserver.c'), str(ROOT/'src/des.c'), str(ROOT/'src/msg-newcamd.c'), str(ROOT/'src/md5.c')], check=True)
    ports = set()
    while len(ports) < 4: ports.add(f.free_port())
    http, cccam, newcamd, card = ports
    cfg = tmp/'isolated.cfg'
    cfg.write_text(f'HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {cccam}\nF: traceuser tracepass\nN: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n[isolated]\nCAID: {caid}\nPROVIDERS: 0\nPORT: {newcamd}\nDCW HALFNULLED: YES\nDCW SWAP: YES\nDCW TIMEOUT: 3000\nUSER: traceuser tracepass\n')
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
            received = re.findall(r'ccdcw: ECM(\d+) CW=([0-9A-F]{32})', result.stdout)
            assert received == [(str(i+1), word) for i, word in enumerate(words)], (received, words, result.stdout)
            for i, tag in enumerate(('80' if n % 2 == 0 else '81' for n in range(len(words))), 1):
                assert f'ECM{i} tag={tag} DCW' in result.stdout, result.stdout
            # The client can read the response before the trace writer records
            # it. Wait briefly for the asynchronous final event, never accept a
            # missing event as success.
            deadline = time.monotonic() + 3
            while True:
                page = request()
                rows = re.findall(r'<tr>.*?</tr>', page, re.DOTALL)
                if sum('CW response sent; picture not verified' in row for row in rows) >= len(words) or time.monotonic() >= deadline:
                    break
                time.sleep(.05)
            assert 'أحداث مستبدلة: 0 | روابط مفقودة: 0 | أحداث مفقودة: 0' in page
            # The summary may repeat the latest evidence outside the table;
            # count actual event rows rather than every occurrence on the page.
            assert sum('Newcamd CW passed initial validation; further gates remain' in row for row in rows) == len(words)
            assert sum('CW response sent; picture not verified' in row for row in rows) == len(words)
            for i, word in enumerate(words, 1):
                attempt_rows = [row for row in rows if re.match(r'<tr><td>[^<]*</td><td>' + str(i) + r'</td>', row)]
                received_rows = [row for row in attempt_rows if 'Newcamd CW response;' in row]
                delivered_rows = [row for row in attempt_rows if 'CW response sent; picture not verified' in row]
                assert len(received_rows) == len(delivered_rows) == 1
                assert word in received_rows[0] and word in delivered_rows[0], (i, word)
            for word in set(words):
                assert page.count(word) == 2 * words.count(word), (word, page.count(word))
            peerlog.flush()
            assert (tmp/'peer.log').read_text(errors='replace').count('NCSERVER: reply ') == len(words)
            assert server.poll() is None and peer.poll() is None
            print('PASS: 16 ' + ('half' if half else 'full') + ' CWs, 15 distinct including one repeat; client bytes and trace match')
        finally:
            for child in reversed(children): f.stop(child)
