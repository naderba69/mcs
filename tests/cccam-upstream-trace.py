#!/usr/bin/env python3
"""Isolated CCcam -> CCcam -> synthetic Newcamd monitor regression.

Only owns and stops the processes it starts; no real card or receiver involved.
"""
import base64, importlib.util, os, re, shutil, subprocess, sys, tempfile, time, urllib.parse, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'src/x64/multics'
spec = importlib.util.spec_from_file_location('monitorfixture', ROOT / 'tests/cccam-trace-success.py')
fixture = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixture)
mode = os.environ.get('MCS_UPSTREAM_RESPONSE', 'good')
short_length = int(os.environ.get('MCS_SHORT_LENGTH', '8'))
assert 0 <= short_length < 16
assert mode in ('good', 'negative', 'bad', 'half', 'short')
caid, sid = ('09BD', '014A') if mode == 'half' else ('1884', '1136')
with tempfile.TemporaryDirectory(prefix='mcs-upstream-cccam-') as folder:
    tmp = Path(folder); cc = tmp / 'ccdcw'; nc = tmp / 'ncserver'
    compiler = os.environ.get('CC', 'cc')
    subprocess.run([compiler, '-O2', '-std=gnu89', '-o', str(cc), str(ROOT/'tests/ccdcw.c')], check=True)
    subprocess.run([compiler, '-O2', '-std=gnu89', '-w', '-o', str(nc), str(ROOT/'tests/ncserver.c'), str(ROOT/'src/des.c'), str(ROOT/'src/msg-newcamd.c'), str(ROOT/'src/md5.c')], check=True)
    ports = set()
    while len(ports) < 7: ports.add(fixture.free_port())
    front_http, back_http, front_cc, back_cc, front_nc, back_nc, card = ports
    backend = tmp/'backend.cfg'; front = tmp/'front.cfg'
    backend.write_text(f'HTTP PORT: {back_http}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {back_cc}\nCCCAM NODEID: 1122334455667788\nF: upstream pass 1 0\nN: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n[backend]\nCAID: {caid}\nPROVIDERS: 0\nPORT: {back_nc}\nDCW TIMEOUT: 3000\nSERVER TIMEOUT: 2500\nDCW HALFNULLED: YES\nDCW SWAP: YES\n' + ('DCWFILTER CHECKSUM: OFF\nENABLE SKIPCWC: NO\n' if mode == 'bad' else ''))
    front.write_text(f'HTTP PORT: {front_http}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\nCCCAM PORT: {front_cc}\nCCCAM NODEID: 1020304050607080\nF: traceuser tracepass\nC: 127.0.0.1 {back_cc} upstream pass\n\n[front]\nCAID: {caid}\nPROVIDERS: 0\nPORT: {front_nc}\nDCW TIMEOUT: 6000\nSERVER TIMEOUT: 5000\nDCW HALFNULLED: YES\nDCW SWAP: YES\nUSER: traceuser tracepass\n')
    # Build a separate malicious fixture; no production source is modified.
    # It advertises a normal card and sends fewer than 16 CW bytes.
    backend_binary = BINARY
    if mode == 'short':
        source = tmp/'src'
        shutil.copytree(ROOT/'src', source, ignore=shutil.ignore_patterns('x64', '__pycache__'))
        (source/'x64').mkdir()
        path = source/'srv-cccam.c'
        text = path.read_text()
        normal = 'cc_msg_send( cli->handle, &cli->sendblock, CC_MSG_ECM_REQUEST, 16, buf)'
        assert text.count(normal) == 1
        path.write_text(text.replace(normal, f'cc_msg_send( cli->handle, &cli->sendblock, CC_MSG_ECM_REQUEST, {short_length}, buf)'))
        build = subprocess.run(['make', '-C', str(source), '-B', 'target=x64', 'link'], capture_output=True, text=True, timeout=120)
        assert build.returncode == 0, build.stderr[-3000:]
        backend_binary = source/'x64/multics'
    children = []; logs = []
    def launch(args, name, env=None):
        log = (tmp / name).open('w+'); logs.append(log)
        p = subprocess.Popen(args, cwd=tmp, env=env, stdout=log, stderr=subprocess.STDOUT)
        children.append(p); return p
    try:
        env = dict(os.environ, NS_DELAYS='60')
        for key in ('NS_NEGATIVE', 'NS_NEGATIVE_FIRST', 'NS_BAD_CHECKSUM', 'NS_HALF_CW', 'NS_OPPOSITE_HALF'):
            env.pop(key, None)
        if mode == 'negative': env['NS_NEGATIVE'] = '1'
        if mode == 'bad': env['NS_BAD_CHECKSUM'] = '1'
        if mode == 'half': env['NS_HALF_CW'] = '1'
        peer = launch([str(nc), str(card), '0102030405060708091011121314', caid, '000000'], 'peer.log', env)
        time.sleep(.3)
        back = launch([str(backend_binary), '-v', '-C', str(backend)], 'backend.log')
        fixture.wait_until(lambda: fixture.connected(back_cc), children)
        frontend = launch([str(BINARY), '-v', '-C', str(front)], 'front.log')
        fixture.wait_until(lambda: fixture.connected(front_cc), children)
        auth = 'Basic '+base64.b64encode(b'test:isolated').decode()
        def get(port, path='/user-trace', data=None):
            req = urllib.request.Request(f'http://127.0.0.1:{port}{path}', data=data, headers={'Authorization':auth})
            with urllib.request.urlopen(req, timeout=5) as r: return r.read().decode()
        fixture.wait_until(lambda: bool(re.search(r'upstream \(ID \d+، متصل', get(back_http))), children, seconds=25)
        page=get(front_http); token=re.search("name='csrf' value='([a-f0-9]{64})'",page); user=re.search("<option value='(\\d+)'[^>]*>traceuser",page)
        assert token and user
        get(front_http, data=urllib.parse.urlencode({'csrf':token.group(1),'action':'start','client':user.group(1),'caid':caid,'provid':'000000','sid':sid}).encode())
        result = subprocess.run([str(cc), '127.0.0.1', str(front_cc), 'traceuser','tracepass',caid,sid,'0x80'], capture_output=True,text=True,timeout=25,check=True)
        page=get(front_http)
        expect = 'DCW' if mode in ('good', 'half') else 'NOK'
        if mode == 'short':
            assert 'ECM1 tag=80 DCW' not in result.stdout, result.stdout
        else:
            assert f'ECM1 tag=80 {expect}' in result.stdout, (result.stdout, page)
        assert 'ECM sent to server' in page, page
        if mode in ('good', 'half'):
            shape = 'zero half present' if mode == 'half' else 'shape not classified'
            assert f'CCcam CW response; {shape}; source response' in page, page
            assert 'CCcam CW passed initial validation; further gates remain' in page, page
            assert 'CW response sent; picture not verified' in page, page
            cw = [0x30 + ((int(sid, 16) + i * 13) & 0x3F) for i in range(16)]
            for q in range(4): cw[q*4+3] = sum(cw[q*4:q*4+3]) & 0xff
            if mode == 'half': cw[8:] = [0] * 8
            assert page.count(''.join(f'{x:02X}' for x in cw)) >= 2, page
        elif mode == 'negative':
            assert 'CCcam NOK1 response; retry may follow; source response' in page, page
            assert '<summary>إظهار CW</summary>' not in page, page
        elif mode == 'short':
            assert 'CCcam malformed CW response: fewer than 16 bytes; source disconnected' in page, page
            assert '<summary>إظهار CW</summary>' not in page, page
            assert frontend.poll() is None
        else:
            assert 'CCcam CW response; shape not classified; source response' in page, page
            assert 'CCcam CW rejected: checksum validation failed' in page, page
            assert page.count('<summary>إظهار CW</summary>') == 1, page
            assert 'CW response sent; picture not verified' not in page, page
        print('PASS: isolated CCcam upstream '+mode + (f' ({short_length} bytes)' if mode == 'short' else ''))
    except Exception:
        for log in logs:
            log.flush(); log.seek(0); print('\n--- '+str(log.name)+' ---\n'+log.read()[-8000:], file=sys.stderr)
        raise
    finally:
        for p in reversed(children): fixture.stop(p)
        for log in logs: log.close()
