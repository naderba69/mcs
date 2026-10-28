#!/usr/bin/env python3
"""Isolated end-to-end CCcam trace with a synthetic Newcamd server.

Usage: python3 tests/cccam-trace-success.py [path-to-multics-binary]
Synthetic CW delivery does not establish that a real receiver has a picture.
Only subprocesses started by this script are stopped.
"""
import base64
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "bin/multics.x64"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_until(predicate, children, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if any(child.poll() is not None for child in children):
            raise RuntimeError("isolated child exited during startup")
        if predicate():
            return
        time.sleep(0.1)
    raise RuntimeError("isolated service did not become ready")


def connected(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            return True
    except OSError:
        return False


def stop(child):
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=5)


def main():
    if not BINARY.is_file():
        raise RuntimeError("binary not found: " + str(BINARY))
    with tempfile.TemporaryDirectory(prefix="mcs-trace-success-") as folder:
        tmp = Path(folder)
        cc, nc = tmp / "ccdcw", tmp / "ncserver"
        compiler = os.environ.get("CC", "cc")
        subprocess.run([compiler, "-O2", "-std=gnu89", "-o", str(cc),
                        str(ROOT / "tests/ccdcw.c")], check=True)
        subprocess.run([compiler, "-O2", "-std=gnu89", "-w", "-o", str(nc),
                        str(ROOT / "tests/ncserver.c"), str(ROOT / "src/des.c"),
                        str(ROOT / "src/msg-newcamd.c"), str(ROOT / "src/md5.c")], check=True)
        ports = set()
        while len(ports) < 4:
            ports.add(free_port())
        http, cccam, newcamd, card = ports
        cfg = tmp / "isolated.cfg"
        cfg.write_text(f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\n"
                       f"TELNET PORT: 0\nCCCAM PORT: {cccam}\nF: traceuser tracepass\n"
                       f"N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n\n"
                       f"[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n"
                       "DCW TIMEOUT: 6000\nUSER: traceuser tracepass\n", encoding="utf-8")
        with (tmp / "peer.log").open("w+") as peerlog, (tmp / "server.log").open("w+") as serverlog:
            env = dict(os.environ, NS_DELAYS="60")
            peer = subprocess.Popen([str(nc), str(card), "0102030405060708091011121314", "1884", "000000"],
                                    cwd=tmp, env=env, stdout=peerlog, stderr=subprocess.STDOUT)
            server = None
            try:
                # Listening check opens a socket; the peer's handshake times out if
                # probed, so let its bind complete before starting the server instead.
                time.sleep(0.3)
                server = subprocess.Popen([str(BINARY), "-v", "-C", str(cfg)], cwd=tmp,
                                          stdout=serverlog, stderr=subprocess.STDOUT)
                wait_until(lambda: connected(cccam), [peer, server])
                def peer_logged_in():
                    peerlog.flush()
                    return "login from user" in (tmp / "peer.log").read_text(errors="replace")
                wait_until(peer_logged_in, [peer, server])
                auth = "Basic " + base64.b64encode(b"test:isolated").decode("ascii")
                url = f"http://127.0.0.1:{http}/user-trace"
                def request(data=None):
                    req = urllib.request.Request(url, data=data, headers={"Authorization": auth})
                    with urllib.request.urlopen(req, timeout=5) as response:
                        return response.read().decode("utf-8")
                page = request()
                token = re.search("name='csrf' value='([a-f0-9]{64})'", page)
                client = re.search("<option value='(\\d+)'[^>]*>traceuser", page)
                assert token and client, "trace form not found"
                form = {"csrf": token.group(1), "action": "start", "client": client.group(1),
                        "caid": "1884", "provid": "000000", "sid": "1136"}
                request(urllib.parse.urlencode(form).encode())
                result = subprocess.run([str(cc), "127.0.0.1", str(cccam), "traceuser", "tracepass",
                                         "1884", "1136", "0x80"], capture_output=True,
                                        text=True, timeout=25, check=True)
                assert "ECM1 tag=80 DCW" in result.stdout, result.stdout
                page = request()
                assert "استلام ECM" in page and "ECM sent to server" in page
                assert "CW response sent; picture not verified" in page
                assert "ECM مشترك" in page
                assert server.poll() is None
                print("PASS: CCcam request, upstream dispatch and CW response traced")
            finally:
                if server is not None:
                    stop(server)
                stop(peer)


if __name__ == "__main__":
    main()
