#!/usr/bin/env python3
"""Exercise complete and truncated CSP PINGREQ extension TLVs over UDP."""
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

binary = Path(sys.argv[1]).resolve()


def free_port(kind):
    sock = socket.socket(socket.AF_INET, kind)
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


with tempfile.TemporaryDirectory(prefix='mcs-pingreq-tlv-') as tmp:
    work = Path(tmp)
    cache_port = free_port(socket.SOCK_DGRAM)
    client_port = free_port(socket.SOCK_STREAM)
    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.bind(('127.0.0.1', 0))
    peer.settimeout(0.2)
    peer_port = peer.getsockname()[1]
    config = work / 'mcs.cfg'
    config.write_text(f'''TELNET PORT: 0
CACHE PORT: {cache_port}
CACHE PEER: 127.0.0.1:{peer_port} {{ csp=1 }}
[test]
CAID: 1884
PROVIDERS: 000000
PORT: {client_port}
USER: u p
''')
    with (work / 'server.log').open('w+') as log:
        process = subprocess.Popen(
            ['stdbuf', '-oL', '-eL', str(binary), '-v', '-C', str(config)],
            cwd=work, stdout=log, stderr=subprocess.STDOUT)
        try:
            # Respond to the server's initial CSP discovery ping.
            deadline = time.monotonic() + 12
            while True:
                assert process.poll() is None, (work / 'server.log').read_text(errors='replace')
                assert time.monotonic() < deadline, 'initial CSP ping timed out'
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

            def pingreq(extension):
                packet = bytearray(13 + len(extension))
                packet[0] = 3
                packet[1:4] = b'MC\x00'
                packet[11:13] = peer_port.to_bytes(2, 'big')
                packet[13:] = extension
                peer.sendto(packet, ('127.0.0.1', cache_port))
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    assert process.poll() is None, (work / 'server.log').read_text(errors='replace')
                    try:
                        response, _ = peer.recvfrom(2048)
                    except socket.timeout:
                        continue
                    if response and response[0] == 4 and response[1:4] == b'MC\x00':
                        return
                raise AssertionError(('PINGREQ response timed out', extension.hex(),
                                      (work / 'server.log').read_text(errors='replace')))

            # The first packet ends immediately after the TLV tag: there is no
            # length byte in the datagram. Then send a valid TLV ending exactly
            # at the datagram boundary to guard compatibility with good peers.
            pingreq(b'\x01')
            pingreq(b'\x01\x03r82')
            assert process.poll() is None, 'server terminated after TLV packets'
            print('PASS: truncated PINGREQ TLV safely ignored; complete TLV still handled')
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            peer.close()
