#!/usr/bin/env python3
"""Live CCcam reconnect test (EPOLL session churn).

Usage: python3 tests/cccam-reconnect.py [path-to-multics-binary]

Proves the server survives repeated CCcam connect/ECM/disconnect cycles
(each cycle is a new TCP session) without crash or hang, including an
abrupt TCP disconnect without handshake. Each cycle must receive a DCW.
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
import urllib.error

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
    caid, sid = "1884", "1136"
    with tempfile.TemporaryDirectory(prefix="mcs-reconnect-") as folder:
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
                       f"N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n"
                       f"\n[ isolated ]\nCAID: {caid}\nPROVIDERS: 0\nPORT: {newcamd}\n"
                       f"DCW TIMEOUT: 3000\nDCW HALFNULLED: YES\nDCW SWAP: YES\n"
                       f"ENABLE SKIPCWC: YES\nDCWFILTER PATTERN: OFF\n"
                       f"USER: traceuser tracepass\n", encoding="utf-8")
        with (tmp / "peer.log").open("w+") as peerlog, (tmp / "server.log").open("w+") as serverlog:
            env = dict(os.environ, NS_DELAYS="60")
            for name in ("NS_HALF_CW", "NS_BAD_CHECKSUM", "NS_NEGATIVE", "NS_OPPOSITE_HALF", "NS_NEGATIVE_FIRST", "NS_CW_SEQUENCE"):
                env.pop(name, None)
            peer = subprocess.Popen([str(nc), str(card), "0102030405060708091011121314", caid, "000000"],
                                    cwd=tmp, env=env, stdout=peerlog, stderr=subprocess.STDOUT)
            server = None
            try:
                time.sleep(0.3)
                server = subprocess.Popen([str(BINARY), "-v", "-C", str(cfg)], cwd=tmp,
                                          stdout=serverlog, stderr=subprocess.STDOUT)
                wait_until(lambda: connected(cccam), [peer, server])

                def peer_logged_in():
                    peerlog.flush()
                    return "login from user" in (tmp / "peer.log").read_text(errors="replace")
                wait_until(peer_logged_in, [peer, server])

                def one_session(tag, label):
                    result = subprocess.run([str(cc), "127.0.0.1", str(cccam), "traceuser", "tracepass",
                                             caid, sid, tag], capture_output=True,
                                            text=True, timeout=25)
                    if result.returncode != 0:
                        serverlog.flush(); peerlog.flush()
                        raise RuntimeError(label + " ccdcw exit " + str(result.returncode)
                                           + " stdout=" + result.stdout + " stderr=" + result.stderr
                                           + "\n---SERVER LOG---\n" + (tmp / "server.log").read_text(errors="replace")[-3000:]
                                           + "\n---PEER LOG---\n" + (tmp / "peer.log").read_text(errors="replace")[-1500:])
                    if "ECM1 tag=" + tag[2:].upper() + " DCW" not in result.stdout:
                        serverlog.flush(); peerlog.flush()
                        trace = ""
                        try:
                            auth = "Basic " + base64.b64encode(b"test:isolated").decode("ascii")
                            req = urllib.request.Request(f"http://127.0.0.1:{http}/user-trace",
                                                         headers={"Authorization": auth})
                            with urllib.request.urlopen(req, timeout=5) as response:
                                trace = response.read().decode("utf-8")
                        except Exception as exc:
                            trace = "trace query failed: " + str(exc)
                        raise AssertionError(label + " stdout=" + result.stdout
                                             + "\n---TRACE (last 2000)---\n" + trace[-2000:]
                                             + "\n---SERVER LOG---\n" + (tmp / "server.log").read_text(errors="replace")[-3000:]
                                             + "\n---PEER LOG---\n" + (tmp / "peer.log").read_text(errors="replace")[-1500:])
                    assert server.poll() is None, label + ": server died during session"
                    print("  session ok: " + label)

                # Three sequential sessions (each a fresh TCP connection).
                # Real clients back off before reconnecting; the server
                # rejects a duplicate login while the old session is still
                # marked connected (60s guard in cc_connect_cli), so allow
                # disconnect detection between sessions.
                one_session("0x80", "first connect")
                time.sleep(0.5)
                one_session("0x81", "reconnect after clean disconnect")
                time.sleep(0.5)
                one_session("0x80", "second reconnect")

                # Abrupt disconnect: TCP connect + immediate close, no handshake.
                with socket.create_connection(("127.0.0.1", cccam), timeout=5):
                    pass
                time.sleep(0.5)
                assert server.poll() is None, "server died on abrupt disconnect"
                print("  abrupt disconnect survived")

                # Server still serves after the abrupt disconnect.
                time.sleep(0.5)
                one_session("0x81", "connect after abrupt disconnect")

                # Web UI still responsive (server not wedged).
                auth = "Basic " + base64.b64encode(b"test:isolated").decode("ascii")
                url = f"http://127.0.0.1:{http}/user-trace"
                req = urllib.request.Request(url, headers={"Authorization": auth})
                with urllib.request.urlopen(req, timeout=5) as response:
                    page = response.read().decode("utf-8")
                assert "traceuser" in page, "web UI lost the client after reconnects"
                assert server.poll() is None
                print("PASS: CCcam reconnect (4 sessions + abrupt disconnect)")
            finally:
                if server is not None:
                    stop(server)
                stop(peer)


if __name__ == "__main__":
    main()
