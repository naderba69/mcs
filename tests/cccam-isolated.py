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
            f"TELNET PORT: 0\nCCCAM PORT: {cccam}\nF: traceuser tracepass\n\n"
            f"[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n"
            "DCW TIMEOUT: 1200\nUSER: traceuser tracepass\n", encoding="utf-8")
        with (temp / "server.log").open("wb") as log:
            server = subprocess.Popen([str(BINARY), "-v", "-C", str(config)],
                                      cwd=temp, stdout=log, stderr=subprocess.STDOUT)
            try:
                wait_for_port(cccam, server)
                client_result = subprocess.run(
                    [str(client), "127.0.0.1", str(cccam), "traceuser", "tracepass",
                     "1884", "1136", "0x80", "0x81"],
                    capture_output=True, text=True, timeout=30, check=True)
                output = client_result.stdout
                if "[FAIL]" in output or "cccam login" not in output:
                    raise AssertionError("CCcam client could not log in: " + output)
                if "ECM1 tag=80 NOK" not in output or "ECM2 tag=81 NOK" not in output:
                    raise AssertionError("expected two NOK responses without upstream: " + output)
                credentials = base64.b64encode(b"test:isolated").decode("ascii")
                request = urllib.request.Request(
                    f"http://127.0.0.1:{http}/log-viewer?level=3&search=1884%3A000000%3A1136",
                    headers={"Authorization": "Basic " + credentials})
                with contextlib.closing(urllib.request.urlopen(request, timeout=5)) as response:
                    page = response.read().decode("utf-8")
                if "</html>" not in page or "1884:000000:1136" not in page:
                    raise AssertionError("log viewer returned an incomplete response")
                if server.poll() is not None:
                    raise AssertionError("server exited during the scenario")
                print("PASS: isolated CCcam login, two NOK responses, log search, server alive")
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
