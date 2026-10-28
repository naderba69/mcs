#!/usr/bin/env python3
"""Exercise a real CCcam request against an isolated local MultiCS instance.

Usage: python3 tests/cccam-isolated.py [path-to-multics-binary]
No upstream servers are configured. A NOK response is expected; it says
nothing about any production channel. The script terminates only its own
child process and keeps temporary credentials and files out of the repo.
"""
import base64
import contextlib
import os
import re
import urllib.error
import urllib.parse
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "bin/multics.x64"


def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_port(port, process):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("isolated server exited before accepting connections")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("isolated server did not start listening")


def run():
    if not BINARY.is_file():
        raise RuntimeError("binary not found: " + str(BINARY))
    with tempfile.TemporaryDirectory(prefix="mcs-cccam-") as tmp:
        temp = Path(tmp)
        client = temp / "ccdcw"
        subprocess.run([os.environ.get("CC", "cc"), "-O2", "-std=gnu89", "-Wall",
                        "-o", str(client), str(ROOT / "tests/ccdcw.c")], check=True)
        # Ports are selected locally; unrelated services are never stopped.
        ports = set()
        while len(ports) < 3:
            ports.add(unused_port())
        http, cccam, newcamd = ports
        config = temp / "isolated.cfg"
        config.write_text(
            f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\n"
            f"TELNET PORT: 0\nCCCAM PORT: {cccam}\nF: traceuser tracepass\nF: otheruser otherpass\n\n"
            f"[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n"
            "DCW TIMEOUT: 1200\nUSER: traceuser tracepass\n", encoding="utf-8")
        with (temp / "server.log").open("wb") as log:
            server = subprocess.Popen([str(BINARY), "-v", "-C", str(config)],
                                      cwd=temp, stdout=log, stderr=subprocess.STDOUT)
            try:
                wait_for_port(cccam, server)
                trace_url = f"http://127.0.0.1:{http}/user-trace"
                credentials = base64.b64encode(b"test:isolated").decode("ascii")
                auth = {"Authorization": "Basic " + credentials}
                def trace_request(data=None):
                    req = urllib.request.Request(trace_url, data=data, headers=auth)
                    with urllib.request.urlopen(req, timeout=5) as result:
                        return result.read().decode("utf-8")
                page = trace_request()
                csrf_match = re.search("name='csrf' value='([a-f0-9]{64})'", page)
                client_match = re.search("<option value='(\d+)'[^>]*>traceuser", page)
                assert csrf_match and client_match, "trace form missing"
                csrf, client_id = csrf_match.group(1), client_match.group(1)
                fields = {"csrf": csrf, "action": "start", "client": client_id,
                          "caid": "1884", "provid": "000000", "sid": "1136"}
                bad = dict(fields, csrf="bad")
                try:
                    trace_request(urllib.parse.urlencode(bad).encode())
                    raise AssertionError("invalid CSRF token was accepted")
                except urllib.error.HTTPError as exc:
                    assert exc.code == 400
                trace_request(urllib.parse.urlencode(fields).encode())
                client_result = subprocess.run(
                    [str(client), "127.0.0.1", str(cccam), "traceuser", "tracepass",
                     "1884", "1136", "0x80", "0x81"],
                    capture_output=True, text=True, timeout=30, check=True)
                output = client_result.stdout
                if "[FAIL]" in output or "cccam login" not in output:
                    raise AssertionError("CCcam client could not log in: " + output)
                if "ECM1 tag=80 NOK" not in output or "ECM2 tag=81 NOK" not in output:
                    raise AssertionError("expected two NOK responses without upstream: " + output)
                page = trace_request()
                assert page.count("استلام ECM") >= 2, "missing received ECM events"
                assert "DCW timeout" in page, "missing timeout reason"
                assert "decode failed; detailed reason unavailable" in page, "missing client result"
                assert "ECM مشترك" in page, "shared events not marked"
                before = page.count("استلام ECM")
                other = subprocess.run(
                    [str(client), "127.0.0.1", str(cccam), "otheruser", "otherpass",
                     "1884", "1136", "0x80"], capture_output=True, text=True,
                    timeout=25, check=True)
                assert "cccam login" in other.stdout and "[FAIL]" not in other.stdout
                assert trace_request().count("استلام ECM") == before, "other client leaked into trace"
                trace_request(urllib.parse.urlencode({"csrf": csrf, "action": "stop"}).encode())
                assert "الحالة: متوقفة" in trace_request(), "trace did not stop"
                trace_request(urllib.parse.urlencode(fields).encode())
                # A reload invalidates client IDs and must stop the session.
                config.write_text(config.read_text(encoding="utf-8"), encoding="utf-8")
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    if "الحالة: متوقفة" in trace_request():
                        break
                    time.sleep(0.2)
                else:
                    raise AssertionError("config reload did not stop trace")
                request = urllib.request.Request(
                    f"http://127.0.0.1:{http}/log-viewer?level=3&search=1884%3A000000%3A1136",
                    headers={"Authorization": "Basic " + credentials})
                with contextlib.closing(urllib.request.urlopen(request, timeout=5)) as response:
                    page = response.read().decode("utf-8")
                if "</html>" not in page or "1884:000000:1136" not in page:
                    raise AssertionError("log viewer returned an incomplete response")
                # An unterminated maximal header must be rejected, not overflow.
                with socket.create_connection(("127.0.0.1", http), timeout=5) as raw:
                    prefix = b"GET / HTTP/1.1\r\nX-Long: "
                    raw.sendall(prefix + b"a" * (2047 - len(prefix)))
                    raw.shutdown(socket.SHUT_WR)
                    raw.recv(64)
                if server.poll() is not None:
                    raise AssertionError("server exited during the scenario")
                print("PASS: CCcam trace, client isolation, CSRF, stop/reload, log search, server alive")
            finally:
                if server.poll() is None:
                    server.terminate()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait(timeout=5)


if __name__ == "__main__":
    run()
