#!/usr/bin/env python3
"""Verify CSP SMS text is rendered without HTML execution or double escaping."""
import base64
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

binary = Path(sys.argv[1]).resolve()


def free_port(kind):
    sock = socket.socket(socket.AF_INET, kind)
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


with tempfile.TemporaryDirectory(prefix='mcs-sms-web-') as tmp:
    work = Path(tmp)
    cache_port = free_port(socket.SOCK_DGRAM)
    http_port = free_port(socket.SOCK_STREAM)
    client_port = free_port(socket.SOCK_STREAM)
    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.bind(('127.0.0.1', 0))
    peer.settimeout(0.25)
    peer_port = peer.getsockname()[1]
    config = work / 'mcs.cfg'
    config.write_text(f'''HTTP PORT: {http_port}
HTTP USER: test
HTTP PASS: isolated
TELNET PORT: 0
CACHE PORT: {cache_port}
CACHE PEER: 127.0.0.1:{peer_port} {{ csp=1 }}
[test]
CAID: 1884
PROVIDERS: 000000
PORT: {client_port}
USER: u p
''')
    process = subprocess.Popen(
        ['stdbuf', '-oL', '-eL', str(binary), '-v', '-C', str(config)],
        cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    authorization = 'Basic ' + base64.b64encode(b'test:isolated').decode()

    def request(path, data=None):
        req = urllib.request.Request(
            f'http://127.0.0.1:{http_port}{path}', data=data,
            headers={'Authorization': authorization})
        with urllib.request.urlopen(req, timeout=4) as response:
            return response.status, response.read().decode(errors='replace')

    try:
        # Complete CSP peer discovery so that inbound SMS/ACKs are accepted.
        deadline = time.monotonic() + 12
        while True:
            assert process.poll() is None, 'server exited during peer discovery'
            assert time.monotonic() < deadline, 'CSP peer discovery timed out'
            try:
                packet, _ = peer.recvfrom(2048)
            except socket.timeout:
                continue
            if packet and packet[0] == 3:
                ack = bytearray(16)
                ack[0] = 0x10 if packet[1:3] == b'MC' else 4
                ack[4:6] = packet[4:6]
                peer.sendto(ack, ('127.0.0.1', cache_port))
                break

        deadline = time.monotonic() + 5
        while True:
            try:
                _, page = request('/cache')
            except (OSError, urllib.error.URLError):
                assert process.poll() is None and time.monotonic() < deadline, 'HTTP server did not start'
                time.sleep(0.05)
                continue
            match = re.search(r'cachepeer\?id=(\d+)', page)
            if match:
                break
            assert process.poll() is None and time.monotonic() < deadline, 'cache peer did not become visible in the authenticated page'
            time.sleep(0.05)
        peer_id = match.group(1)

        # Locally composed text is raw in memory and is the security regression.
        outbound = b'<img src=x onerror=alert(1)> & "quote"'
        post_result = []

        def send_http_sms():
            try:
                post_result.append(request(f'/cachepeer?action=sms&id={peer_id}', outbound))
            except Exception as exc:  # propagated below after the socket ACK attempt
                post_result.append(exc)

        sender = threading.Thread(target=send_http_sms)
        sender.start()
        deadline = time.monotonic() + 5
        while True:
            assert time.monotonic() < deadline, 'outgoing CSP SMS not received'
            try:
                packet, _ = peer.recvfrom(2048)
            except socket.timeout:
                continue
            if packet and packet[0] == 0x14:
                assert packet[5:] == outbound, 'CSP payload changed before transmission'
                peer.sendto(b'\x15' + packet[1:5], ('127.0.0.1', cache_port))
                break
        sender.join(timeout=5)
        assert not sender.is_alive(), 'HTTP SMS action did not finish after CSP ACK'
        assert len(post_result) == 1 and not isinstance(post_result[0], Exception), post_result
        assert post_result[0][0] == 200, post_result

        _, page = request(f'/cachepeer?id={peer_id}')
        outbound_html = '&lt;img src=x onerror=alert(1)&gt; &amp; &quot;quote&quot;'
        assert outbound_html in page, 'outbound text was not safely HTML-escaped'
        assert '<img src=x onerror=alert(1)>' not in page, 'raw HTML injection appeared in the page'

        # Inbound CSP text is escaped at ingress; rendering must not escape it twice.
        inbound = b'<svg/onload=alert(2)>& "incoming"'
        inbound_hash = bytes.fromhex('c0ffee42')
        peer.sendto(b'\x14' + inbound_hash + inbound, ('127.0.0.1', cache_port))
        deadline = time.monotonic() + 4
        expected_inbound = '&lt;svg/onload=alert(2)&gt;&amp; &quot;incoming&quot;'
        while True:
            try:
                packet, _ = peer.recvfrom(2048)
                if packet == b'\x15' + inbound_hash:
                    break
            except socket.timeout:
                assert time.monotonic() < deadline, 'inbound SMS acknowledgement timed out'
        deadline = time.monotonic() + 3
        while True:
            _, page = request(f'/cachepeer?id={peer_id}')
            assert expected_inbound in page, 'inbound message was double-escaped or lost'
            assert '&amp;lt;svg' not in page, 'inbound HTML entity was escaped a second time'
            break
        print('PASS: outgoing SMS is escaped; inbound SMS remains escaped exactly once')
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        peer.close()
