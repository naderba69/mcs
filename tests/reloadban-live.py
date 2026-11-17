#!/usr/bin/env python3
"""Band 77 live proof: saving the config from the web editor must not
kick/ban a same-IP CCcam device or the operator's browser.

Field report: the operator edits the file in the browser; at the save
moment his receiver -- a CCcam client on the SAME source IP -- gets
kicked and the IP is temporarily banned. Three root causes, all fixed:

  1. CCcam counted every connection ATTEMPT (band 60, IPLIST
     semantics): a receiver dropped by the reload reconnects into the
     duplicate-login guard (old session still marked connected), each
     reject counted, none can succeed to clear -- IP banned. Band 77
     counts only real credential failures (unknown user, wrong
     password, wrong nodeid); a duplicate reject verifies credentials
     and CLEARS the count instead.
  2. No reload shield: during reread_config every door's per-IP
     counters saw reload churn as brute force. Band 77 suspends
     failure counting on all doors during the reload + 10s grace
     (bans already in force still hold).
  3. The editor saved with fopen(fname,"w") -- the config sat
     truncated on disk during the write. It now writes a temp file
     and renames atomically; the watcher reloads on the rename.

Phases:
  A held CCcam session + editor save + duplicate-reconnect churn:
    no ban, browser fine, held session completes, fresh session ok.
  B wrong-password burst INSIDE the post-save grace: still no ban
    (shield); after the grace expires, the same burst bans cccam
    only -- the gate is not weakened, just reload-aware.
"""
import base64, re, signal, socket, subprocess, sys, tempfile, time
import urllib.request, urllib.error
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests'
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
KEY = '0102030405060708091011121314'


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


def http_get(http):
    auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
    r = urllib.request.Request(f'http://127.0.0.1:{http}/user-trace', headers={'Authorization': auth})
    try:
        with urllib.request.urlopen(r, timeout=5) as resp:
            return resp.status, resp.read().decode('utf-8', errors='replace')
    except urllib.error.HTTPError as e:
        return e.code, ''


def editor_save(http, text):
    boundary = '----mcsband77boundary'
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="textedit"\r\n\r\n'
            + text + f'\r\n--{boundary}--\r\n').encode()
    auth = 'Basic ' + base64.b64encode(b'admin:admin').decode()
    r = urllib.request.Request(f'http://127.0.0.1:{http}/editor0', data=body,
                               headers={'Authorization': auth,
                                        'Content-Type': f'multipart/form-data; boundary={boundary}'})
    with urllib.request.urlopen(r, timeout=10) as resp:
        page = resp.read().decode('utf-8', errors='replace')
    assert 'Successfully Saved' in page, 'editor save not confirmed: ' + page[-300:]


def session(ccbin, cport, user='devuser', pw='devpass', tags=('0x80',)):
    out = subprocess.run([str(ccbin), '127.0.0.1', str(cport), user, pw, '1884', '1136', *tags],
                         capture_output=True, text=True, timeout=60)
    return out.stdout.count('DCW')


def active_bans(http):
    for attempt in range(2):
        st, page = http_get(http)
        assert st == 200, f'browser locked out (HTTP {st})'
        m = re.search(r'نشط الآن: (\d+)', page)
        if m: return int(m.group(1)), page
        time.sleep(1)
    raise AssertionError('protoban section missing on /user-trace')


def main():
    ccbin, ncbin = TESTS / '.ccdcw.bin', TESTS / '.ncserver.bin'
    subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(ccbin), str(TESTS / 'ccdcw.c')], check=True)
    if not ncbin.exists():
        subprocess.run(['cc', '-O2', '-std=gnu89', '-w', '-o', str(ncbin), str(TESTS / 'ncserver.c'),
                        str(ROOT / 'src/des.c'), str(ROOT / 'src/msg-newcamd.c'), str(ROOT / 'src/md5.c')], check=True)

    td = tempfile.TemporaryDirectory(prefix='mcs-relban-')
    work = Path(td.name)
    http, cport, pport, card = (free_port() for _ in range(4))
    cfg = work / 'one.cfg'
    base = '\n'.join([
        f'HTTP PORT: {http}', 'HTTP USER: admin', 'HTTP PASS: admin',
        'HTTP LOGIN FAILS: 5', 'HTTP LOGIN BAN: 3600',
        'TELNET PORT: 0',
        f'CCCAM PORT: {cport}', 'F: devuser devpass',
        'CCCAM LOGIN FAILS: 5', 'CCCAM LOGIN BAN: 3600',
        f'N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14',
        '', '[ relban ]', f'PORT: {pport}', 'CAID: 1884', 'PROVIDERS: 0',
        'USER: devuser devpass', ''])
    cfg.write_text(base)
    cardp = subprocess.Popen([str(ncbin), str(card), KEY, '1884', '000000'],
                             stdout=open(work / 'card.log', 'w'), stderr=subprocess.STDOUT,
                             env={'NS_DELAYS': '400', 'PATH': '/usr/bin:/bin'})
    srvp = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(cfg)],
                            stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
    try:
        deadline = time.monotonic() + 20
        while 'advertised card' not in (work / 'card.log').read_text(errors='replace'):
            if time.monotonic() > deadline: raise AssertionError('card never connected')
            time.sleep(0.2)
        assert session(ccbin, cport) == 1, 'baseline CCcam session failed'
        print('reloadban-live: baseline session ok')
        time.sleep(2)   # let the server notice the baseline disconnect
                        # (duplicate-login guard) before the held session

        # --- Phase A: held session across an editor save + churn.
        # The held client is frozen with SIGSTOP inside its first ECM
        # wait (card delay 400ms): its socket stays open exactly like a
        # receiver's established session while the operator saves.
        tags = [f'0x{0x80 + (i % 2):02x}' for i in range(30)]
        held = subprocess.Popen([str(ccbin), '127.0.0.1', str(cport), 'devuser', 'devpass',
                                 '1884', '1136', *tags],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(0.25)
        held.send_signal(signal.SIGSTOP)
        assert held.poll() is None, 'held session died before save'
        editor_save(http, base.replace('[ relban ]', '# touched by editor\r\n[ relban ]'))
        assert '# touched by editor' in cfg.read_text(), 'atomic save did not persist'
        assert not (work / 'one.cfg.tmp').exists(), 'temp file left behind by editor save'
        for i in range(10):   # duplicate-login rejects while held session lives
            session(ccbin, cport)
            st, _ = http_get(http)
            assert st == 200, 'browser locked out during churn'
        n, _ = active_bans(http)
        assert n == 0, f'reload churn produced an IP ban (active={n}) -- band 77 regression'
        held.kill()
        held.wait(timeout=5)
        time.sleep(1.5)       # server notices the dropped session
        assert session(ccbin, cport) == 1, 'fresh session after reload not served'
        print('reloadban-live: phase A PASS (save across held session, churn unbanned, browser ok)')

        # --- Phase B: shield window, then the gate bites after grace
        editor_save(http, cfg.read_text().replace('# touched by editor', '# touched again'))
        for i in range(6):
            session(ccbin, cport, pw='wrongpass')
        n, _ = active_bans(http)
        assert n == 0, 'failures inside the reload grace were counted -- shield broken'
        print('reloadban-live: phase B1 PASS (grace shields counting)')
        time.sleep(12)   # LOGIN_RELOAD_GRACE_MS = 10s
        for i in range(6):
            session(ccbin, cport, pw='wrongpass')
        n, page = active_bans(http)
        assert n == 1 and re.search(r'cccam — 127\.0\.0\.1', page), \
            'brute force after grace did NOT ban -- gate weakened!'
        st, _ = http_get(http)
        assert st == 200, 'web door must stay open (per-protocol ban)'
        print('reloadban-live: phase B2 PASS (gate still bans cccam only, after grace)')
    finally:
        for p in (srvp, cardp): p.kill()
        for p in (srvp, cardp): p.wait(timeout=5)
        td.cleanup()
    print('reloadban-live: PASS')


if __name__ == '__main__':
    main()
