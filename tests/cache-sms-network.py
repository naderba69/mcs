#!/usr/bin/env python3
"""Exercise CSP SMS ingress over UDP; no real peer or message content required."""
import os, select, socket, subprocess, sys, tempfile, time, urllib.request
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
def free_port(kind):
    s = socket.socket(socket.AF_INET, kind)
    s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p

with tempfile.TemporaryDirectory(prefix='mcs-sms-') as tmp:
    d = Path(tmp)
    # The executable basename determines the SMS file under /var/tmp.
    exe = d / d.name
    exe.symlink_to(binary)
    sms_file = Path('/var/tmp') / (d.name + '.sms')
    cp, hp, nc = free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM), free_port(socket.SOCK_STREAM)
    peers = []
    for _ in range(2):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0)); peers.append(sock)
    ports = [sock.getsockname()[1] for sock in peers]
    reputation = d / 'reputation'
    reputation.write_text(f'127.0.0.1:{ports[1]} 3 60 seeded\n')
    cfg = d / 'server.cfg'
    cfg.write_text(f'''HTTP PORT: {hp}
HTTP USER: test
HTTP PASS: isolated
TELNET PORT: 0
PEER REPUTATION: ON
PEER REPUTATION FILE: {reputation}
CACHE PORT: {cp}
CACHE PEER: 127.0.0.1:{ports[0]} {{ csp=1 }}
CACHE PEER: 127.0.0.1:{ports[1]} {{ csp=1 }}
[test]
CAID: 1884
PROVIDERS: 000000
PORT: {nc}
USER: testuser testpass
''')
    with (d / 'log').open('w+') as log:
        process = subprocess.Popen(['stdbuf', '-oL', '-eL', str(exe), '-v', '-C', str(cfg)], cwd=d, stdout=log, stderr=subprocess.STDOUT)
        try:
            # The server's ping reply registers each UDP source as an existing peer.
            online = set(); deadline = time.monotonic() + 15
            while len(online) != 2:
                assert process.poll() is None, ('server exited during handshake', (d/'log').read_text(errors='replace'))
                assert time.monotonic() < deadline, 'handshake timeout'
                for sock in select.select(peers, [], [], .2)[0]:
                    packet, _ = sock.recvfrom(2048)
                    if len(packet) >= 9 and packet[0] == 3:
                        ack = bytearray(16)
                        ack[0] = 0x10 if packet[1:3] == b'MC' else 4
                        ack[4:6] = packet[4:6]
                        sock.sendto(ack, ('127.0.0.1', cp)); online.add(sock.fileno())
            deadline = time.monotonic() + 3
            while 'ban restored from the reputation file' not in (d/'log').read_text(errors='replace'):
                assert process.poll() is None and time.monotonic() < deadline, 'ban not applied'
                time.sleep(.03)
            def send_and_wait(sock, ident, text, expect_ack):
                sock.sendto(b'\x14' + ident + text, ('127.0.0.1', cp))
                end = time.monotonic() + .8; found = False
                while time.monotonic() < end:
                    if select.select([sock], [], [], .1)[0]:
                        data, _ = sock.recvfrom(2048)
                        if data == b'\x15' + ident: found = True
                assert found == expect_ack, (ident.hex(), found, expect_ack)
                assert process.poll() is None, 'server terminated'
            # 170 escaped quotes occupy 1020 of 1024 bytes; 171 cannot fit.
            send_and_wait(peers[0], b'\x01\x02\x03\x04', b'"'*170, True)
            send_and_wait(peers[0], b'\x01\x02\x03\x05', b'"'*171, False)
            send_and_wait(peers[0], b'\x01\x02\x03\x06', b'"'*507, False)
            send_and_wait(peers[1], b'\x01\x02\x03\x07', b'banned', False)
            send_and_wait(peers[0], b'\x01\x02\x03\x08', b'normal <&>', True)
            text = sms_file.read_text()
            assert '&quot;'*170 in text and 'normal &lt;&amp;&gt;' in text
            assert 'banned' not in text
            print('PASS: bounded SMS, banned peer, ACK and continued operation')
        finally:
            process.terminate()
            try: process.wait(timeout=3)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
            for sock in peers: sock.close()
            sms_file.unlink(missing_ok=True)
