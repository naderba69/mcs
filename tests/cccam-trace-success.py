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
    mode = os.environ.get("MCS_TEST_RESPONSE", "full")
    assert mode in ("full", "half", "bad", "negative", "excluded", "swap", "wronghalf", "retry", "late", "non09half", "recycled")
    caid = "1884" if mode in ("full", "late", "non09half", "recycled") else "09BD"
    sid = "1136" if mode in ("full", "late", "non09half", "recycled") else "014A"
    tag = os.environ.get("MCS_TEST_TAG", "0x80")
    assert tag in ("0x80", "0x81")
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
                       f"[ isolated ]\nCAID: {caid}\nPROVIDERS: 0\nPORT: {newcamd}\n"
                       + ("DCW TIMEOUT: 1000\n" if mode == "late" else "DCW TIMEOUT: 3000\n") + "DCW HALFNULLED: YES\n"
                       + ("DCW SWAP: NO\n" if mode == "wronghalf" else "DCW SWAP: YES\n")
                       +
                       "ENABLE SKIPCWC: YES\nDCWFILTER PATTERN: OFF\n"
                       + (f"DCWFILTER CHECKSUM: OFF\nDCW SKIPCWC_EXCLUDE_SIDS_ACTIVE: YES\nDCW SKIPCWC_EXCLUDE_SIDLIST: {sid}\n" if mode == "excluded" else "")
                       + ("RETRY NEWCAMD: 1\n" if mode == "retry" else "")
                       + "USER: traceuser tracepass\n", encoding="utf-8")
        with (tmp / "peer.log").open("w+") as peerlog, (tmp / "server.log").open("w+") as serverlog:
            env = dict(os.environ, NS_DELAYS="60")
            for name in ("NS_HALF_CW", "NS_BAD_CHECKSUM", "NS_NEGATIVE", "NS_OPPOSITE_HALF", "NS_NEGATIVE_FIRST"):
                env.pop(name, None)
            if mode not in ("full", "late", "recycled"): env["NS_HALF_CW"] = "1"
            if mode in ("bad", "excluded"): env["NS_BAD_CHECKSUM"] = "1"
            if mode == "negative": env["NS_NEGATIVE"] = "1"
            if mode == "retry": env["NS_NEGATIVE_FIRST"] = "1"
            if mode in ("swap", "wronghalf"): env["NS_OPPOSITE_HALF"] = "1"
            peer = subprocess.Popen([str(nc), str(card), "0102030405060708091011121314", caid, "000000"],
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
                try:
                    urllib.request.urlopen(url, timeout=5)
                    raise AssertionError("unauthenticated trace access succeeded")
                except urllib.error.HTTPError as exc:
                    assert exc.code in (401, 403)
                page = request()
                token = re.search("name='csrf' value='([a-f0-9]{64})'", page)
                client = re.search("<option value='(\\d+)'[^>]*>traceuser", page)
                assert token and client, "trace form not found"
                form = {"csrf": token.group(1), "action": "start", "client": client.group(1),
                        "caid": caid, "provid": "000000", "sid": sid}
                if os.environ.get("MCS_TRACE_ALL") == "1":
                    form = {"csrf": token.group(1), "action": "start", "client": client.group(1), "mode": "all"}
                request(urllib.parse.urlencode(form).encode())
                result = subprocess.run([str(cc), "127.0.0.1", str(cccam), "traceuser", "tracepass",
                                         caid, sid, tag], capture_output=True,
                                        text=True, timeout=25, check=True)
                expected = "TIMEOUT" if mode in ("late", "recycled") else ("DCW" if mode in ("full", "half", "excluded", "swap", "retry") else "NOK")
                assert "ECM1 tag=" + tag[2:].upper() + " " + expected in result.stdout, result.stdout
                page = request()
                assert "استلام ECM" in page and "ECM sent to server" in page
                assert "cache lookup invoked; pipe delivery and result not confirmed" in page
                assert "حركة كاش" in page
                if mode in ("full", "late", "recycled"): assert "Newcamd CW response; validity not determined" in page
                if mode in ("half", "bad", "excluded", "swap", "wronghalf", "retry", "non09half"): assert "Newcamd CW response; zero half present; validity not determined" in page
                if mode == "bad": assert "Newcamd CW rejected: checksum validation failed" in page
                if mode in ("negative", "retry"): assert "Newcamd negative response; retry may follow" in page
                if mode == "retry": assert "Newcamd retry send reported success" in page
                if mode == "non09half":
                    assert "Newcamd CW passed initial validation; further gates remain" in page
                    reason = "CW rejected: zero half not allowed for CAID family"
                    if os.environ.get("MCS_TEST_QUEUE_TRACE") in ("zero", "different"):
                        assert reason not in page
                    else:
                        assert reason in page
                if mode == "wronghalf": assert "CW rejected: half does not match ECM cycle and swap disabled" in page
                if expected == "DCW":
                    assert "Newcamd CW passed initial validation; further gates remain" in page
                    assert "CW response sent; picture not verified" in page
                elif mode == "recycled":
                    assert "Newcamd CW passed initial validation; further gates remain" in page
                    assert "CW response sent; picture not verified" not in page
                elif mode == "late":
                    assert "Newcamd CW passed initial validation; further gates remain" in page
                    assert "CW committed; client dispatch skipped after DCW timeout" in page
                    assert "CW response sent; picture not verified" not in page
                    assert "decode failed; detailed reason unavailable" not in page
                else: assert "decode failed; detailed reason unavailable" in page
                assert "ECM مشترك" in page
                assert "source response " in page
                assert f"{caid}:000000:{sid}" in page
                if mode in ("full", "half"):
                    # Fixture key only: ensure both source and client rows carry
                    # the actual same key, not merely a display placeholder.
                    key = [0x30 + ((int(sid, 16) + i * 13) & 0x3F) for i in range(16)]
                    for q in range(4): key[q * 4 + 3] = sum(key[q * 4:q * 4 + 3]) & 0xFF
                    if mode == "half":
                        if tag == "0x81": key[:8] = [0] * 8
                        else: key[8:] = [0] * 8
                    encoded = "".join(f"{x:02X}" for x in key)
                    assert page.count(encoded) >= 2
                if mode in ("full", "half", "excluded", "swap", "retry"):
                    assert page.count("<summary>إظهار CW</summary>") >= 2
                elif mode in ("bad", "wronghalf", "late", "non09half", "recycled"):
                    assert page.count("<summary>إظهار CW</summary>") == 1
                else:
                    assert "<summary>إظهار CW</summary>" not in page
                assert server.poll() is None
                # Starting a new session must not expose events or CW from the old one.
                request(urllib.parse.urlencode(form).encode())
                cleared = request()
                assert "<summary>إظهار CW</summary>" not in cleared
                assert "Newcamd CW response" not in cleared
                print("PASS: CCcam/Newcamd trace mode=" + mode)
            finally:
                if server is not None:
                    stop(server)
                stop(peer)


if __name__ == "__main__":
    main()
