#!/usr/bin/env python3
"""Experimental scoped checksum policy: reload during a delayed source response.

Pass a binary containing the experimental policy. Synthetic CWs do not prove
real broadcasts decode. The script owns only its temporary child processes.
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
BINARY = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location("tracefixture", ROOT / "tests/cccam-trace-success.py")
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)

with tempfile.TemporaryDirectory(prefix="scoped-reload-") as folder:
    tmp = Path(folder)
    cc, nc = tmp / "ccdcw", tmp / "ncserver"
    compiler = os.environ.get("CC", "cc")
    subprocess.run([compiler, "-O2", "-std=gnu89", "-o", str(cc), str(ROOT / "tests/ccdcw.c")], check=True)
    subprocess.run([compiler, "-O2", "-std=gnu89", "-w", "-o", str(nc), str(ROOT / "tests/ncserver.c"), str(ROOT / "src/des.c"), str(ROOT / "src/msg-newcamd.c"), str(ROOT / "src/md5.c")], check=True)
    ports = set()
    while len(ports) < 4: ports.add(f.free_port())
    http, cccam, newcamd, card = ports
    config = tmp / "isolated.cfg"
    base = (f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\nTELNET PORT: 0\n"
            f"CCCAM PORT: {cccam}\nF: traceuser tracepass\n"
            f"N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n"
            f"\n[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n"
            "DCW TIMEOUT: 4500\nSERVER TIMEOUT: 4000\nDCW HALFNULLED: YES\nDCW SWAP: YES\nENABLE SKIPCWC: YES\n"
            "DCWFILTER PATTERN: OFF\n{policy}USER: traceuser tracepass\n")
    config.write_text(base.format(policy="CW_CHECKSUM_EXEMPT: 000000:1136\n"))
    logs = []
    children = []
    def launch(cmd, name, env=None):
        log = (tmp / name).open("w+")
        logs.append(log)
        proc = subprocess.Popen(cmd, cwd=tmp, env=env, stdout=log, stderr=subprocess.STDOUT)
        children.append(proc)
        return proc
    try:
        env = dict(os.environ, NS_DELAYS="3000", NS_BAD_CHECKSUM="1", NS_VERBOSE="1")
        for key in ("NS_HALF_CW", "NS_NEGATIVE", "NS_OPPOSITE_HALF", "NS_NEGATIVE_FIRST", "NS_CW_SEQUENCE"):
            env.pop(key, None)
        peer = launch(["stdbuf", "-oL", "-eL", str(nc), str(card), "0102030405060708091011121314", "1884", "000000"], "peer.log", env)
        time.sleep(.3)
        server = launch(["stdbuf", "-oL", "-eL", str(BINARY), "-v", "-C", str(config)], "server.log")
        f.wait_until(lambda: f.connected(cccam), children)
        f.wait_until(lambda: "login from user" in (tmp / "peer.log").read_text(errors="replace"), children)
        auth = "Basic " + base64.b64encode(b"test:isolated").decode()
        url = f"http://127.0.0.1:{http}/user-trace"
        def request(data=None):
            req = urllib.request.Request(url, data=data, headers={"Authorization": auth})
            with urllib.request.urlopen(req, timeout=10) as response:
                return response.read().decode("utf-8")
        def start_trace():
            page = request()
            token = re.search("name='csrf' value='([a-f0-9]{64})'", page)
            client = re.search("<option value='(\\d+)'[^>]*>traceuser", page)
            assert token and client, "trace form not found"
            form = {"csrf":token.group(1), "action":"start", "client":client.group(1), "caid":"1884", "provid":"000000", "sid":"1136"}
            request(urllib.parse.urlencode(form).encode())
        def run_ecm(seed, expected):
            result = subprocess.run([str(cc), "127.0.0.1", str(cccam), "traceuser", "tracepass", "1884", "1136", f"0x80:{seed}"], capture_output=True, text=True, timeout=25, check=True)
            assert "ECM1 tag=80 " + expected in result.stdout, (result.stdout, request())
            page = request()
            if expected == "NOK": assert "Newcamd CW rejected: checksum validation failed" in page
            else: assert "Newcamd CW passed initial checks under scoped checksum exemption; validity and picture unknown" in page
        def reload(policy):
            # The config thread stops an active trace and holds its config lock
            # through reload; a subsequent GET waits for completion of reload.
            config.write_text(base.format(policy=policy))
            f.wait_until(lambda: "الحالة: متوقفة" in request(), children, 20)
            start_trace()
        start_trace()
        # Wait for the request-specific dispatch evidence, not an arbitrary
        # sleep, before removing the exception while its source is delayed.
        client = subprocess.Popen([str(cc), "127.0.0.1", str(cccam), "traceuser", "tracepass", "1884", "1136", "0x80:11"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        children.append(client)
        f.wait_until(lambda: "ECM sent to server" in request(), children, 8)
        config.write_text(base.format(policy=""))
        output, _ = client.communicate(timeout=20)
        assert client.returncode == 0 and "ECM1 tag=80 NOK" in output, output
        f.wait_until(lambda: "الحالة: متوقفة" in request(), [peer, server], 20)
        assert "NCSERVER: reply 1" in (tmp / "peer.log").read_text(errors="replace"), "synthetic source did not confirm its reply"
        print("PASS: exception removed after dispatch; delayed source replied and no CW was delivered")

    except Exception:
        for log in logs:
            log.flush(); log.seek(0)
            print("\n--- " + str(log.name) + " ---\n" + log.read()[-10000:], file=sys.stderr)
        raise
    finally:
        for child in reversed(children): f.stop(child)
        for log in logs: log.close()
