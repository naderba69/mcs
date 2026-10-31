#!/usr/bin/env python3
"""Real CCcam CacheEX regression for provider-collision handling.

A local CCcam ECM requests provider 0. A real CCcam CacheEX peer first pushes
provider 1 (must be refused before validation/storage/delivery), then provider
0 as a matching control (must reach the client). Both decisions are checked in
the web trace. Synthetic CW delivery does not establish real source quality or
picture output.
"""
import base64
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "make-x64/x64/multics"
CW = "01020306112233660A0B0C21515253F6"
COLLISION = "cache provider collision: explicit incoming provider conflicts with known cache/local provider; CW refused"
FORWARDED = "cache forwarded CW to ECM pipe; delivery not confirmed"

spec = importlib.util.spec_from_file_location("trace_helpers", ROOT / "tests/cccam-trace-success.py")
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def reserve_ports(count):
    ports = set()
    while len(ports) < count:
        ports.add(helpers.free_port())
    return list(ports)


def run_case(tmp, peer_binary, client_binary, provider, expect_delivery):
    http, cache, cccam = reserve_ports(3)
    config = tmp / f"provider-{provider}.cfg"
    config.write_text(
        f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\n"
        f"TELNET PORT: 0\nCACHE PORT: {cache}\nCACHE FILTER: OFF\nCACHE THRESHOLD: 1\n"
        f"CCCAM PORT: {cccam}\nF: u1 p1\nF: cxu cxp {{ cacheex_mode=3; }}\n\n"
        f"[ provider-test ]\nCAID: 1884\nPROVIDERS: 000000, 000001\nPORT: 0\n"
        "ENABLE CACHEEX: YES\nDCW TIMEOUT: 3000\nCACHE TIMEOUT: 1000\n"
        "USER: u1 p1\n", encoding="utf-8")

    server = subprocess.Popen([str(BINARY), "-C", str(config)], cwd=tmp,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    client = None
    peer_output = ""
    client_output = ""
    last_page = ""
    try:
        auth = "Basic " + base64.b64encode(b"test:isolated").decode("ascii")
        url = f"http://127.0.0.1:{http}/user-trace"

        def request(data=None):
            req = urllib.request.Request(url, data=data, headers={"Authorization": auth})
            with urllib.request.urlopen(req, timeout=4) as response:
                return response.read().decode("utf-8")

        def ready():
            try:
                req = urllib.request.Request(f"http://127.0.0.1:{http}/",
                                             headers={"Authorization": auth})
                with urllib.request.urlopen(req, timeout=0.4):
                    return True
            except Exception:
                return False

        helpers.wait_until(ready, [server], seconds=15)
        client_env = dict(os.environ, CCDCW_PROVID="000000", CCDCW_PRINT_CW="1",
                          CCDCW_PRE_ECM_DELAY_MS="7000")
        client = subprocess.Popen(
            [str(client_binary), "127.0.0.1", str(cccam), "u1", "p1", "1884", "64", "0x80"],
            cwd=tmp, env=client_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

        def user_visible():
            return bool(re.search("<option value='(\\d+)'[^>]*>u1", request()))

        helpers.wait_until(user_visible, [server, client], seconds=10)
        page = request()
        token = re.search("name='csrf' value='([a-f0-9]{64})'", page)
        user = re.search("<option value='(\\d+)'[^>]*>u1", page)
        assert token and user, "authenticated CCcam client not selectable in web trace"
        request(urllib.parse.urlencode({
            "csrf": token.group(1), "action": "start", "client": user.group(1), "mode": "all"
        }).encode())

        # Wait until the real ECM is both pending and visible in the web trace.
        deadline = time.monotonic() + 10
        saw_lookup = False
        while time.monotonic() < deadline:
            if client.poll() is not None:
                client_output = client.stdout.read()
                raise AssertionError("CCcam client exited before the CacheEX push: " + client_output)
            last_page = request()
            if "cache lookup invoked; pipe delivery and result not confirmed" in last_page:
                saw_lookup = True
                break
            time.sleep(0.1)
        assert saw_lookup, "the web trace did not show the pending ECM cache lookup"

        peer = subprocess.run(
            [str(peer_binary), "127.0.0.1", str(cccam), "cxu", "cxp",
             f"push 1884 {provider} 64 {CW}"],
            cwd=tmp, capture_output=True, text=True, timeout=10, check=True)
        peer_output = peer.stdout + peer.stderr
        assert "[ ok ] cccam login" in peer_output, peer_output
        assert "[ ok ] push" in peer_output, peer_output

        client_output, _ = client.communicate(timeout=15)
        last_page = request()
        if expect_delivery:
            assert "ECM1 tag=80 DCW" in client_output, client_output
            assert "CW=" + CW in client_output, client_output
            assert FORWARDED in last_page, last_page
            assert COLLISION not in last_page, last_page
            print("PASS: matching CacheEX provider 000000 reached the pending CCcam request")
        else:
            assert "ECM1 tag=80 DCW" not in client_output, client_output
            assert "ECM1 tag=80 NOK" in client_output or "ECM1 tag=80 TIMEOUT" in client_output, client_output
            assert COLLISION in last_page, last_page
            assert "رُفضت CW لأن provider الوارد صراحة يخالف" in last_page, last_page
            print("PASS: mismatching CacheEX provider 000001 was refused and shown in the web trace")
    finally:
        if client is not None:
            helpers.stop(client)
        helpers.stop(server)


def main():
    if not BINARY.is_file():
        raise RuntimeError("server binary not found: " + str(BINARY))
    with tempfile.TemporaryDirectory(prefix="mcs-cacheex-provider-") as folder:
        tmp = Path(folder)
        compiler = os.environ.get("CC", "cc")
        peer_binary = tmp / "cxpeer"
        client_binary = tmp / "ccdcw"
        subprocess.run([compiler, "-O2", "-std=gnu89", "-o", str(peer_binary),
                        str(ROOT / "tests/cxpeer.c")], check=True)
        subprocess.run([compiler, "-O2", "-std=gnu89", "-o", str(client_binary),
                        str(ROOT / "tests/ccdcw.c")], check=True)
        run_case(tmp, peer_binary, client_binary, "1", False)
        run_case(tmp, peer_binary, client_binary, "0", True)


if __name__ == "__main__":
    main()
