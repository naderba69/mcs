"""Compare isolated cache-policy cases and verify scoped results in CCcam trace.

Only synthetic CWs and local child processes are used. A received CW here does
not prove a real control word or a visible picture.
"""
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


def port(sock_type):
    with socket.socket(socket.AF_INET, sock_type) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def stop(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


def http_request(url, auth, data=None):
    request = urllib.request.Request(url, data=data, headers=auth)
    with urllib.request.urlopen(request, timeout=5) as response:
        return response.read().decode(errors='replace')


with tempfile.TemporaryDirectory(prefix='mcs-csp-comp-') as folder:
    tmp = pathlib.Path(folder)
    peer = tmp / 'peer'
    newcamd_client = tmp / 'ncclient'
    cccam_client = tmp / 'ccdcw'

    subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(peer),
                    str(ROOT / 'tests/cachepeer.c')], check=True)
    subprocess.run(['cc', '-O2', '-std=gnu89', '-w', '-o', str(newcamd_client),
                    str(ROOT / 'tests/ncclient.c'), str(ROOT / 'src/des.c'),
                    str(ROOT / 'src/msg-newcamd.c'), str(ROOT / 'src/md5.c')], check=True)
    subprocess.run(['cc', '-O2', '-std=gnu89', '-o', str(cccam_client),
                    str(ROOT / 'tests/ccdcw.c')], check=True)

    cases = [
        ('bare-no', 'FILTER: NO\nFILTER TIME: 10\n'),
        ('legacy-filter', 'FILTER: 5\nFILTER TIME: 150\n'),
        ('explicit-off', 'CACHE FILTER: OFF\n'),
        ('standard-off', 'CACHE PROTECTION: STANDARD\nCACHE FILTER: OFF\n'),
        ('scoped-checksum', 'CACHE FILTER: OFF\n'),
        ('scoped-mismatch', 'CACHE FILTER: OFF\n'),
    ]
    selected_case = os.environ.get('MCS_CACHE_1884_CASE')
    if selected_case and selected_case not in {case for case, _ in cases}:
        raise SystemExit(f'unknown MCS_CACHE_1884_CASE: {selected_case}')

    for name, global_policy in cases:
        if selected_case and name != selected_case:
            continue
        http_port = port(socket.SOCK_STREAM)
        newcamd_port = port(socket.SOCK_STREAM)
        cccam_port = port(socket.SOCK_STREAM)
        cache_port = port(socket.SOCK_DGRAM)
        peer_port = port(socket.SOCK_DGRAM)
        config = tmp / 'config'
        scoped_exemption = (
            'CW_CHECKSUM_EXEMPT: 000000:1136\n'
            if name == 'scoped-checksum' else
            'CW_CHECKSUM_EXEMPT: 000000:9999\n'
            if name == 'scoped-mismatch' else ''
        )
        config.write_text(
            f'HTTP PORT: {http_port}\n'
            'HTTP USER: test\n'
            'HTTP PASS: test\n'
            'TELNET PORT: 0\n'
            f'CCCAM PORT: {cccam_port}\n'
            'F: traceuser tracepass\n'
            f'CACHE PORT: {cache_port}\n'
            f'CACHE PEER: 127.0.0.1:{peer_port} {{ csp=1 }}\n'
            f'{global_policy}\n'
            '[cachetest]\n'
            'CAID: 1884\n'
            'PROVIDERS: 000000\n'
            f'PORT: {newcamd_port}\n'
            'DCW HALFNULLED: NO\n'
            'DCW CHECK: YES\n'
            'DCW MINTIME: 0\n'
            'DCWFILTER PATTERN: OFF\n'
            'ENABLE CACHE: YES\n'
            'CACHE TIMEOUT: 500\n'
            'CACHE STATIC: NO\n'
            'SERVER TIMEOUT: 5000\n'
            f'{scoped_exemption}'
            'USER: u1 p1\n'
            'USER: traceuser tracepass\n'
        )

        peer_log = tmp / f'{name}-peer.log'
        server_log = tmp / f'{name}-server.log'
        with peer_log.open('w+') as peer_file, server_log.open('w+') as server_file:
            peer_cw = (
                '11223367445566FF77889998AABBCC31'
                if name in ('scoped-checksum', 'scoped-mismatch')
                else '11223366445566FF77889998AABBCC31'
            )
            peer_env = {key: value for key, value in os.environ.items()
                        if not key.startswith('CP_')}
            peer_proc = subprocess.Popen(
                ['stdbuf', '-o0', '-e0', str(peer), str(cache_port),
                 str(peer_port), peer_cw, '5'],
                stdout=peer_file, stderr=subprocess.STDOUT, cwd=tmp, env=peer_env,
            )
            server_proc = None
            try:
                time.sleep(0.2)
                server_proc = subprocess.Popen(
                    ['stdbuf', '-o0', '-e0', str(BINARY), '-C', str(config), '-v'],
                    stdout=server_file, stderr=subprocess.STDOUT, cwd=tmp,
                )

                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if peer_proc.poll() is not None or server_proc.poll() is not None:
                        raise RuntimeError(f'{name}: test child exited during startup')
                    if 'advertised card' in peer_log.read_text(errors='replace'):
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError(f'{name}: cache peer handshake failed')

                newcamd_result = subprocess.run(
                    [str(newcamd_client), '127.0.0.1', str(newcamd_port),
                     'u1', 'p1', '0102030405060708091011121314', '0', '1884', '1136'],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, timeout=30,
                )
                peer_output = peer_log.read_text(errors='replace')
                server_output = server_log.read_text(errors='replace')
                assert newcamd_result.returncode == 0, (
                    name, 'Newcamd fixture failed', newcamd_result.stdout
                )

                auth = {
                    'Authorization': 'Basic ' + base64.b64encode(b'test:test').decode()
                }
                cache_url = f'http://127.0.0.1:{http_port}/cache'
                cache_page = http_request(cache_url, auth)
                expected_filter = 'ON' if name in ('bare-no', 'legacy-filter') else 'OFF'
                assert f'Filter = {expected_filter}' in cache_page, (
                    name, 'unexpected filter state', cache_page
                )
                assert 'got request sid 1136' in peer_output and 'PEER: replied CW' in peer_output, (
                    name, 'cache peer did not receive and answer the request', peer_output
                )

                expected_deliveries = (
                    2 if name in ('explicit-off', 'standard-off', 'scoped-checksum') else 0
                )
                actual_deliveries = newcamd_result.stdout.count('DELIVERED A CONTROL WORD')
                assert actual_deliveries == expected_deliveries, (
                    name, newcamd_result.stdout
                )

                if name in ('scoped-checksum', 'scoped-mismatch'):
                    # The trace UI lists CCcam users, so drive this observation
                    # through the real CCcam test client rather than Newcamd.
                    trace_url = f'http://127.0.0.1:{http_port}/user-trace'
                    trace_page = http_request(trace_url, auth)
                    csrf_match = re.search(
                        "name='csrf' value='([a-f0-9]{64})'", trace_page
                    )
                    client_match = re.search(
                        "<option value='(\\d+)'[^>]*>traceuser", trace_page
                    )
                    assert csrf_match and client_match, (
                        name, 'CCcam trace form or configured user missing', trace_page
                    )
                    trace_form = {
                        'csrf': csrf_match.group(1),
                        'action': 'start',
                        'client': client_match.group(1),
                        'caid': '1884',
                        'provid': '000000',
                        'sid': '1136',
                    }
                    http_request(
                        trace_url, auth,
                        urllib.parse.urlencode(trace_form).encode(),
                    )
                    cccam_result = subprocess.run(
                        [str(cccam_client), '127.0.0.1', str(cccam_port),
                         'traceuser', 'tracepass', '1884', '1136', '0x81'],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True, timeout=15,
                    )
                    assert cccam_result.returncode == 0 and 'cccam login as' in cccam_result.stdout, (
                        name, cccam_result.returncode, cccam_result.stdout
                    )
                    trace_page = http_request(trace_url, auth)
                    peer_output = peer_log.read_text(errors='replace')
                    assert peer_output.count('got request sid 1136') >= 2, (
                        name, 'CCcam ECM did not trigger a distinct cache-peer request', peer_output
                    )
                    if name == 'scoped-checksum':
                        assert 'ECM1 tag=81 DCW' in cccam_result.stdout, (
                            name, cccam_result.stdout
                        )
                        assert ('cache CW passed profile checks under scoped checksum '
                                'exemption; validity and picture unknown') in trace_page, (
                            name, 'missing scoped cache trace', trace_page
                        )
                        assert 'اجتازت CW القادمة من الكاش فحوص البروفايل باستثناء checksum المحدد' in trace_page, (
                            name, 'missing Arabic scoped trace', trace_page
                        )
                    else:
                        assert 'ECM1 tag=81 DCW' not in cccam_result.stdout, (
                            name, 'checksum-mismatched CW was delivered', cccam_result.stdout
                        )
                        assert 'cache rejected: profile CW checksum validation failed' in trace_page, (
                            name, 'missing checksum rejection trace', trace_page
                        )

                if name == 'standard-off':
                    assert '[CONSENSUS] holding cw' in server_output, (
                        name, 'no consensus hold observed', server_output
                    )

                print(
                    name,
                    'newcamd_delivered=', actual_deliveries,
                    'peer_requests=', peer_output.count('request'),
                    'client_exit=', newcamd_result.returncode,
                    'consensus=', server_output.count('CONSENSUS'),
                )
                print(
                    f'  PASS: filter {expected_filter}; synthetic delivery and cache request observed'
                )
            finally:
                stop(server_proc)
                stop(peer_proc)
