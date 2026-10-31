#!/usr/bin/env python3
"""Smoke-test the optional Radegast server's TCP read/dispatch path."""
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

binary = Path(sys.argv[1]).resolve()


port_reservations = []


def free_port(kind):
    # Keep reservations alive until every port is chosen so the OS cannot
    # hand the same just-released port to a second listener in this test.
    sock = socket.socket(socket.AF_INET, kind)
    sock.bind(('127.0.0.1', 0))
    port_reservations.append(sock)
    return sock.getsockname()[1]


with tempfile.TemporaryDirectory(prefix='mcs-radegast-smoke-') as tmp:
    work = Path(tmp)
    radegast_port = free_port(socket.SOCK_STREAM)
    newcamd_port = free_port(socket.SOCK_STREAM)
    http_port = free_port(socket.SOCK_STREAM)
    for reservation in port_reservations:
        reservation.close()
    port_reservations.clear()
    config = work / 'mcs.cfg'
    config.write_text(f'''HTTP PORT: {http_port}
TELNET PORT: 0
CACHE PORT: 0
[test]
CAID: 1884
PROVIDERS: 000000
PORT: {newcamd_port}
RADEGAST PORT: {radegast_port}
ACCEPT NULL SID: YES
USER: u p
''')
    with (work / 'server.log').open('w+') as log:
        process = subprocess.Popen(
            ['stdbuf', '-oL', '-eL', str(binary), '-v', '-C', str(config)],
            cwd=work, stdout=log, stderr=subprocess.STDOUT)
        client = None

        def receive_exact(size):
            data = bytearray()
            while len(data) < size:
                try:
                    chunk = client.recv(size - len(data))
                except socket.timeout as error:
                    log_text = (work / 'server.log').read_text(errors='replace')
                    raise AssertionError('timed out waiting for a Radegast response; server log:\n' + log_text) from error
                if not chunk:
                    raise AssertionError('Radegast closed before completing a response')
                data.extend(chunk)
            return bytes(data)

        def wait_for_log(needle, timeout, message):
            deadline = time.monotonic() + timeout
            while needle not in (work / 'server.log').read_text(errors='replace'):
                assert process.poll() is None, (work / 'server.log').read_text(errors='replace')
                assert time.monotonic() < deadline, message
                time.sleep(0.02)

        try:
            deadline = time.monotonic() + 25
            while True:
                assert process.poll() is None, (work / 'server.log').read_text(errors='replace')
                assert time.monotonic() < deadline, 'Radegast listener did not start'
                candidate = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                candidate.settimeout(20)
                try:
                    candidate.connect(('127.0.0.1', radegast_port))
                    client = candidate
                    break
                except OSError:
                    candidate.close()
                    time.sleep(0.05)

            # A wire reply proves the listener accepted and dispatched this
            # request; do not gate the test on a particular debug-log line.
            invalid_tlv = bytes([10, 2, 0x99, 0x99])
            client.sendall(bytes([1, len(invalid_tlv)]) + invalid_tlv)
            failure = receive_exact(4)
            assert failure == bytes([2, 2, 4, 0]), ('unexpected denial', failure.hex())

            # Split the frame header across TCP writes, then send a TLV that
            # ends after its tag and lacks the mandatory length byte.
            client.sendall(bytes([1]))
            time.sleep(0.02)
            client.sendall(bytes([1, 10]))
            malformed = receive_exact(4)
            assert malformed == bytes([2, 2, 4, 0]), ('unexpected malformed-packet reply', malformed.hex())

            # A profile-approved ECM with null SID enabled must reach dispatch.
            accepted_tlvs = (
                bytes([10, 2, 0x18, 0x84]) +
                bytes([6, 6]) + b'000000' +
                bytes([3, 4, 0x80, 0x00, 0x01, 0x02])
            )
            client.sendall(bytes([1, len(accepted_tlvs)]) + accepted_tlvs)
            wait_for_log(' <- ecm from client ', 8,
                         'accepted ECM did not reach the Radegast handler')
            print('PASS: Radegast rejects an unavailable CAID, safely parses a split/malformed frame, and dispatches an accepted ECM')
        finally:
            if client is not None:
                client.close()
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
