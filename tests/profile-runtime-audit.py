#!/usr/bin/env python3
"""Boot every shipped profile on temporary ports, then read its actual web fields.

This does not edit the repository configuration or contact a provider. It
replaces management and profile ports and management credentials only in a
private temporary copy. Output has no passwords, raw ECMs, or CWs.
"""
import argparse, base64, html, json, os, re, secrets, socket, subprocess, tempfile, time, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--config', type=Path, default=ROOT/'multics-r24-optimized.cfg')
parser.add_argument('--binary', type=Path, default=ROOT/'src/x64/multics')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()

lines = args.config.read_text(encoding='utf-8').splitlines(keepends=True)
# Refuse unknown keys: no real peers, client accounts or source definitions.
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT/'tests'))
import cfgcheck
global_keys, parsed_profiles = cfgcheck.parse(args.config)
allowed_global = set("CACHE PROTECTION|CACHE ATTEST TTL|DCW STATS|PEER REPUTATION|PEER REPUTATION DISTRUST|PEER REPUTATION ISOLATE|PEER REPUTATION BAN|HTTP LOGIN DELAY|TELNET LOGIN DELAY|DEFAULT KEY|DEFAULT DCW CHECK|DEFAULT DCW MAXFAILED|DEFAULT DCW RETRY|DEFAULT DCW CYCLE_CHECK|DEFAULT ENABLE SKIPCWC|DEFAULT CACHE STATIC|DEFAULT CACHE TIMEOUT|DEFAULT CACHE SENDREQ|DEFAULT CACHE RESENDREQ|DEFAULT CACHE SENDREP|DEFAULT SERVER FIRST|DEFAULT SERVER TIMEOUT|DEFAULT SERVER INTERVAL|DEFAULT SERVER VALIDECMTIME|DEFAULT SERVER MAX|CAID FAMILY HALFNULLED|CAID FAMILY PROVIDERCHECK|CAID FAMILY NOPROVCHK|CACHE FILTER|CACHE FILTER TIME|DEFAULT CACHEEX VALIDECMTIME|DEFAULT CACHEEX MAXHOP|DEFAULT CACHEEX MAXHOP_LG|DEFAULT CACHEEX LOCAL_ONLY|DEFAULT CACHEEX CWCHECK|DEFAULT CACHEEX BLOCK_FAKE_CW|DEFAULT ACCEPT NULL SID|DEFAULT ACCEPT NULL CAID|DEFAULT ACCEPT NULL PROVIDER|DEFAULT ENABLE CCCAM|DEFAULT ENABLE NEWCAMD|DEFAULT ENABLE RADEGAST|DEFAULT ENABLE CACHE|DEFAULT ENABLE CAMD35|DEFAULT ENABLE CS378X|DEFAULT ENABLE MGCAMD|DEFAULT ENABLE FREECCCAM|DEFAULT ENABLE CACHEEX|HTTP PORT|HTTP USER|HTTP PASS|TELNET PORT|CACHE PORT".split('|'))
allowed_profile = set("PORT|CAID|CA MODERN|PROVIDERS|DCW HALFNULLED|DCW CHECK|DCWFILTER PATTERN|CACHE TIMEOUT|ENABLE CACHEEX|SERVER TIMEOUT|DCW MINTIME|DCW MAXFAILED|DCW RETRY|DCW CYCLE_CHECK|SERVER VALIDECMTIME|ENABLE SKIPCWC|ACCEPT NULL PROVIDER|CACHE STATIC|SERVER FIRST|ONID|SERVER INTERVAL|ECM CHECK LENGTH|ENABLE CACHE|CACHE RESENDREQ|CACHE SENDREQ|CACHE SENDREP|CACHEEX MAXHOP_LG|CACHEEX VALIDECMTIME|SERVER MAX|ECM LENGTH|ENABLE CS378X|ENABLE CAMD35|DCW SWAP|ENABLE CCCAM|ENABLE NEWCAMD|ENABLE MGCAMD|SID LIST|SID LIST !|ACCEPT NULL SID|ACCEPT NULL CAID|ENABLE SOFTCAM|DCW TIMEOUT|SKIPCWC_ONLY_SIDS_ACTIVE|SKIPCWC_ONLY_SIDLIST|SKIPCWC_EXCLUDE_SIDS_ACTIVE|SKIPCWC_EXCLUDE_SIDLIST|DCW SKIPCWC_EXCLUDE_SIDS_ACTIVE|DCW SKIPCWC_EXCLUDE_SIDLIST|ENABLE RADEGAST|ECM HEADERWLST".split('|'))
unknown = set(global_keys) - allowed_global
for p in parsed_profiles: unknown.update(set(p['kv']) - allowed_profile)
if unknown: raise SystemExit('Fixture refuses unknown or network directives: ' + ', '.join(sorted(unknown)))

profiles = []
for lineno, line in enumerate(lines, 1):
    match = re.match(r'^\[([^\]]+)\]', line)
    if match:
        profiles.append({'name':match.group(1), 'line':lineno, 'keys':{}})
    elif profiles and re.match(r'^[A-Za-z][^:#]*:', line):
        key, value = line.split(':', 1)
        profiles[-1]['keys'].setdefault(key.strip().upper(), []).append(value.strip())
if not profiles or any(len(p['keys'].get('PORT', [])) != 1 for p in profiles):
    raise SystemExit('Every profile must have exactly one PORT')
if len({p['name'] for p in profiles}) != len(profiles):
    raise SystemExit('Duplicate profile names')
# Hold allocated ports until the entire list is reserved, to avoid duplicates.
held = []
for _ in range(len(profiles)+2):
    sock = socket.socket(); sock.bind(('127.0.0.1', 0)); held.append(sock)
ports = [sock.getsockname()[1] for sock in held]
http, cache, *profile_ports = ports
for sock in held: sock.close()

rendered, i, in_profile = [], 0, False
user, password = secrets.token_hex(16), secrets.token_hex(24)
replacements = {'HTTP PORT':str(http), 'HTTP USER':user, 'HTTP PASS':password,
                'TELNET PORT':'0', 'CACHE PORT':str(cache)}
seen = set()
for line in lines:
    if line.startswith('['): in_profile = True
    match = re.match(r'^([A-Za-z][^:#]*):', line)
    if match:
        key = match.group(1).strip().upper()
        if not in_profile and key in replacements:
            rendered.append(key + ': ' + replacements[key] + '\n'); seen.add(key); continue
        if in_profile and key == 'PORT':
            rendered.append('PORT: ' + str(profile_ports[i]) + '\n'); i += 1; continue
    rendered.append(line)
assert set(replacements) <= seen and i == len(profiles)

auth = 'Basic ' + base64.b64encode(f'{user}:{password}'.encode()).decode()
def get(path):
    req = urllib.request.Request(f'http://127.0.0.1:{http}{path}', headers={'Authorization':auth})
    with urllib.request.urlopen(req, timeout=5) as response:
        return response.read().decode('utf-8', errors='replace')

def stop(child):
    if child.poll() is None:
        child.terminate()
        try: child.wait(timeout=4)
        except subprocess.TimeoutExpired:
            child.kill(); child.wait(timeout=4)

with tempfile.TemporaryDirectory(prefix='mcs-profile-audit-') as folder:
    tmp = Path(folder); config = tmp/'audit.cfg'; config.write_text(''.join(rendered), encoding='utf-8')
    with (tmp/'server.log').open('w+') as log:
        child = subprocess.Popen([str(args.binary.resolve()), '-v', '-C', str(config)], cwd=tmp, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic()+35
            while True:
                if child.poll() is not None: raise RuntimeError('isolated server exited at startup')
                try: page = get('/profiles'); break
                except (OSError, urllib.error.URLError):
                    if time.monotonic() >= deadline: raise
                    time.sleep(.2)
            links = re.findall(r'<a href="/profile\?id=(\d+)">([^<]+)</a>', page)
            ids = {html.unescape(name): int(id_) for id_, name in links}
            assert len(ids) == len(profiles), (len(ids),len(profiles))
            results = []
            mismatches = []
            for profile, port in zip(profiles, profile_ports):
                name = profile['name']; assert name in ids, name
                page = get('/profile?id=' + str(ids[name]))
                actual_name = re.search(r'Profile: ([^<]+)</b>', page)
                caid = re.search(r'<li>Caid = ([0-9A-F]{4})</li>', page)
                actual_port = re.search(r'<li>Newcamd Port = (\d+)</li>', page)
                assert actual_name and html.unescape(actual_name.group(1)) == name
                assert caid and actual_port and int(actual_port.group(1)) == port
                fields = dict((html.unescape(k),html.unescape(v)) for k,v in re.findall(r'<tr><td>([^<]+)</td><td>([^<]+)</td></tr>', page))
                assert 'SERVER TIMEOUT' in fields and 'CACHE TIMEOUT' in fields
                assert int(profile['keys']['CAID'][-1].split('#')[0].split(',')[0].strip(), 16) == int(caid.group(1), 16)
                for key, values in profile['keys'].items():
                    if key not in fields: continue
                    expected = values[-1].split('#')[0].strip().upper()
                    actual = fields[key].upper().removesuffix('MS').strip()
                    if expected != actual: mismatches.append([name, key, expected, actual])
                results.append({'name':name, 'line':profile['line'], 'id':ids[name], 'caid':caid.group(1),
                                'configured_port':profile['keys']['PORT'][-1], 'configured':profile['keys'], 'web_fields':fields})
            # Check passive listen state without connecting to or blocking 72 login handlers.
            listening = set()
            for table in ('/proc/net/tcp', '/proc/net/tcp6'):
                if not os.path.exists(table): continue
                for line in Path(table).read_text().splitlines()[1:]:
                    parts = line.split()
                    if parts[3] == '0A': listening.add(int(parts[1].split(':')[1], 16))
            missing = sorted(set(profile_ports) - listening)
            assert not missing, ('temporary ports not listening', missing)
            assert child.poll() is None
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({'profile_count':len(results), 'temporary_ports_listening':len(profile_ports),
                                                'mismatches':mismatches, 'profiles':results}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print(f'PASS: {len(results)} profiles parsed, web fields read, {len(profile_ports)} temporary ports listening; {len(mismatches)} field mismatches')
            for mismatch in mismatches: print('  MISMATCH:', *mismatch)
            if mismatches: raise SystemExit('web/config mismatches detected')
        finally: stop(child)
