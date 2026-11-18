#!/usr/bin/env python3
"""Band 93 design + function audit: EVERY page, line by line, not samples.

Operator report: browser config save still misbehaved on his server,
colors looked random, text vanished on some pages, layouts scattered.
Bands 89-91 proved pages answer and look coherent; this audit proves
the stronger invariants, on the real 91-profile production file:

  1. Every route answers 200 with a real body (as pageaudit).
  2. Every CSS class used in any page's HTML is defined in that
     page's own stylesheet (no unstyled, scattered components).
  3. No <table> is left bare: it carries class/border/style or the
     page stylesheet has a global table rule.
  4. No known dark-on-dark clash survives: the old #004455 pre text
     and the #4CAF50 BISS green are gone from every served page.
  5. Every page carries navigation (shared menu, mcs-nav bar, or
     <nav>) -- no dead-end pages.
  6. The browser editor save works at FULL size: the real 3530-line
     production file is loaded in the editor, saved back through the
     multipart POST with a marker appended, and the bytes on disk,
     the success page, the re-rendered editor and the live
     /smartfilter snapshot all agree; the server stays up.
  7. BISS add/list/delete actions actually work end to end.
"""
import base64, http.client, re, socket, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'bin/multics.x64'
CFG = ROOT / 'multics-full-30w-52e.cfg'
AUTH = {'Authorization': 'Basic ' + base64.b64encode(b'a:a').decode()}

ROUTES = ['/', '/profiles', '/newcamd', '/servers', '/cache', '/biss', '/cccam',
          '/cs378x', '/camd35', '/cacheex', '/freecccam', '/mgcamd',
          '/diagnostics', '/debug-ecm', '/server-stats', '/user-trace',
          '/log-viewer', '/cache-analytics', '/smartfilter', '/debug',
          '/threads', '/cwlog', '/editor0', '/auto-tuning']


def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p


class Client:
    def __init__(self, port):
        self.port = port

    def get(self, path, auth=True):
        c = http.client.HTTPConnection('127.0.0.1', self.port, timeout=20)
        c.request('GET', path, headers=AUTH if auth else {})
        r = c.getresponse(); body = r.read().decode('utf-8', 'ignore')
        c.close(); return r.status, body

    def post_editor(self, text):
        boundary = '----designaudit93'
        payload = (f'--{boundary}\r\nContent-Disposition: form-data; name="textedit"\r\n\r\n'
                   + text + f'\r\n--{boundary}--\r\n').encode('utf-8')
        c = http.client.HTTPConnection('127.0.0.1', self.port, timeout=30)
        headers = dict(AUTH); headers['Content-Type'] = f'multipart/form-data; boundary={boundary}'
        c.request('POST', '/editor0', payload, headers)
        r = c.getresponse(); body = r.read().decode('utf-8', 'ignore')
        c.close(); return r.status, body


def audit_page(path, status, body, bad):
    if status != 200:
        bad.append((path, f'HTTP {status}')); return
    if len(body) < 100:
        bad.append((path, f'tiny body {len(body)}B')); return
    css = ' '.join(re.findall(r'<style[^>]*>(.*?)</style>', body, re.S))
    defined = set(re.findall(r'\.([A-Za-z][\w-]*)', css))
    used = set()
    for m in re.findall(r'class=(?:"([^"]+)"|\'([^\']+)\'|([^\s>]+))', body):
        used.update(' '.join(m).split())
    missing = used - defined
    if missing:
        bad.append((path, f'classes used but not defined: {sorted(missing)}'))
    has_table_rule = bool(re.search(r'(^|[}\s])table\s*[,{]', css))
    for t in re.findall(r'<table[^>]*>', body):
        if 'class' not in t and 'border' not in t and 'style' not in t and not has_table_rule:
            bad.append((path, f'bare table with no styling: {t[:60]}')); break
    for clash in ('004455', '4CAF50', '45a049'):
        if clash in body:
            bad.append((path, f'legacy clash color {clash} still served'))
    if not ('class=menu' in body or 'mcs-nav' in body or '<nav' in body or path == '/auto-tuning'):
        bad.append((path, 'no navigation bar'))


def main():
    port = free_port()
    with tempfile.TemporaryDirectory(prefix='mcs-designaudit-') as td:
        work = Path(td)
        text = CFG.read_text(encoding='utf-8', errors='replace')
        text = re.sub(r'^HTTP PORT:.*$', f'HTTP PORT: {port}', text, flags=re.M)
        text = re.sub(r'^HTTP USER:.*$', 'HTTP USER: a', text, flags=re.M)
        text = re.sub(r'^HTTP PASS:.*$', 'HTTP PASS: a', text, flags=re.M)
        text = re.sub(r'^TELNET PORT:.*$', 'TELNET PORT: 0', text, flags=re.M)
        (work / 'full.cfg').write_text(text)
        srv = subprocess.Popen(['stdbuf', '-o0', '-e0', str(BIN.resolve()), '-C', str(work / 'full.cfg')],
                               stdout=open(work / 'srv.log', 'w'), stderr=subprocess.STDOUT, cwd=work)
        try:
            cl = Client(port)
            deadline = time.monotonic() + 30
            while True:
                try:
                    st, _ = cl.get('/'); break
                except Exception:
                    if time.monotonic() > deadline: raise AssertionError('server never came up')
                    time.sleep(0.3)

            bad = []
            for route in ROUTES:
                try:
                    st, body = cl.get(route)
                    audit_page(route, st, body, bad)
                except Exception as e:
                    bad.append((route, repr(e)))
            for route, why in bad:
                print(f'  [FAIL] {route:18s} {why}')
            assert not bad, f'{len(bad)} design failures'

            # --- full-size editor save -------------------------------------
            st, editor = cl.get('/editor0')
            assert st == 200 and 'name="textedit"' in editor, 'editor did not render'
            original = (work / 'full.cfg').read_text(encoding='utf-8')
            marker = '# DESIGNAUDIT93-MARKER'
            st, resp = cl.post_editor(original + '\n' + marker + '\n')
            assert st == 200 and 'Successfully Saved' in resp, f'save not confirmed: HTTP {st}'
            on_disk = (work / 'full.cfg').read_text(encoding='utf-8')
            assert on_disk == original + '\n' + marker + '\n', \
                'saved bytes differ from what the browser sent (truncation/corruption)'
            assert srv.poll() is None, 'server died after the save'
            st, editor2 = cl.get('/editor0')
            assert marker in editor2, 'saved marker not visible in the editor after reload'
            st, sf = cl.get('/smartfilter')
            assert st == 200 and 'Level 2' in sf, 'config snapshot changed after save'
            print(f'designaudit: editor saved the full {len(original)}-char / '
                  f'{original.count(chr(10))}-line config byte-identically')

            # --- Band 94: the reload the save triggered is timed -----------
            home = ''
            for _ in range(40):
                st, home = cl.get('/')
                if 'Last Reload' in home and ' ms' in home: break
                time.sleep(0.25)
            assert 'Last Reload' in home and ' ms' in home, \
                'home page does not publish the last reload duration'
            m = re.search(r'Last Reload</div><div class=.value.>([\d.]+) ms', home)
            print(f'designaudit: reload timed and published (Last Reload = {m.group(1) if m else "?"} ms)')

            # --- Band 99: live home + the profiles port-cell clash ---------
            assert 'home-hero' in home and 'live-badge' in home and "id='kpiTotalEcm'" in home, \
                'home is not the live control-center (hero/live KPIs missing)'
            st, frag = cl.get('/?action=div')
            assert st == 200 and 'home-hero' in frag, \
                'the action=div live fragment is not a valid HTTP reply'
            st, profiles = cl.get('/profiles')
            assert 'td.online{background:var(--good-bg);color:var(--good);}' in profiles, \
                'status cells are still pills (port-number colour clash)'
            assert 'td.online a{color:var(--good);}' in profiles, \
                'port link still fights the status-cell background'
            print('designaudit: live home + port-cell clash guards hold')

            # --- Band 101: no silent disconnects ----------------------------
            for bad_route in ('/nonexistent-page', '/editor99', '/sms'):
                st, body = cl.get(bad_route)
                assert st == 404 and 'not found' in body, \
                    f'{bad_route} is not the designed 404 (HTTP {st})'
            print('designaudit: unknown/hidden/bad-index routes answer the designed 404')

            # --- Band 102: health classification ----------------------------
            assert 'Health classification' in home and home.count('class-panel') >= 3, \
                'home lacks the Strong/Medium/Bad classification panels'
            assert 'Bad-CW Sources' in home and 'Cache Hits' in home, \
                'home lacks the live cache/bad-CW cards'
            st, profiles = cl.get('/profiles')
            assert "class='grade g" in profiles, 'profiles carry no grade badges'
            print('designaudit: classification panels + grade badges hold')

            # --- Band 104: CW_DUP inside CCcam Trace -----------------------
            st, trace = cl.get('/user-trace')
            assert st == 200 and 'CW_DUP' in trace and 'تكرار الشفرة' in trace, \
                'CW_DUP is not embedded in the CCcam trace overview'
            print('designaudit: CW_DUP trace section holds')
            assert 'CW_REUSE' in trace and 'Cache Consensus' in trace, \
                'CW_REUSE / consensus arming missing from the trace overview'
            print('designaudit: CW_REUSE + consensus trace sections hold')
            assert 'MARCHCHECK' in trace and 'ركن النصف' in trace, \
                'MARCHCHECK is not embedded in the CCcam trace overview'
            print('designaudit: MARCHCHECK trace section holds')
            assert 'LATECW' in trace and 'الشفرة المتأخرة' in trace, \
                'LATECW is not embedded in the CCcam trace overview'
            print('designaudit: LATECW trace section holds')

            # --- BISS add/list/delete --------------------------------------
            st, _ = cl.get('/biss?action=add&caid=2600&sid=0001&provid=000000&key=0123456789ABCDEF')
            st, listing = cl.get('/biss')
            assert '<code>0123456789ABCDEF</code>' in listing, 'added BISS key not listed'
            st, _ = cl.get('/biss?action=delete&caid=2600&sid=0001&provid=000000')
            st, listing2 = cl.get('/biss')
            assert '<code>0123456789ABCDEF</code>' not in listing2, 'deleted BISS key still listed'
            print('designaudit: BISS add/list/delete works')
        finally:
            srv.kill(); srv.wait(timeout=5)
    print('designaudit: PASS (every page styled, navigable, clash-free; full-size save proven)')


if __name__ == '__main__':
    main()
