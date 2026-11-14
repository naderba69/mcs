#!/usr/bin/env python3
"""Live DoubleCheck + field-status proof on the CCcam trace page.

Two synthetic Newcamd card servers answer the same channel. The first
is slow, so the ECM is retried/failed over to the fast second server;
both CWs eventually reach the commit path for the same ECM hash, which
is exactly what the observe-mode DoubleCheck compares. MODE=agree:
both servers return the same CW (default sid-derived key) -> the trace
page must show a two-source agreement. MODE=mismatch: the fast server
returns a different (checksum-valid) CW -> a mismatch decision must
appear. No real picture is asserted anywhere; decisions are read from
/user-trace, the operator's single page.

Usage: python3 tests/doublecheck-live.py [path-to-multics-binary]
Env:   MODE=agree|mismatch
"""
import base64, os, re, socket, subprocess, sys, tempfile, time
import urllib.parse, urllib.request, urllib.error
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "bin/multics-r82a-r22-x64"
MODE = os.environ.get("MODE", "agree")
assert MODE in ("agree", "mismatch")

def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0)); return s.getsockname()[1]

def wait_until(pred, children, seconds=20):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if any(c.poll() is not None for c in children):
            raise RuntimeError("child exited during startup")
        if pred(): return
        time.sleep(0.1)
    raise RuntimeError("service did not become ready")

def connected(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2): return True
    except OSError:
        return False

def stop(c):
    if c and c.poll() is None:
        c.terminate()
        try: c.wait(timeout=5)
        except subprocess.TimeoutExpired: c.kill(); c.wait(timeout=5)

def main():
    assert BINARY.is_file(), BINARY
    with tempfile.TemporaryDirectory(prefix="mcs-doublecheck-") as folder:
        tmp = Path(folder)
        cc, nc = tmp / "ccdcw", tmp / "ncserver"
        subprocess.run(["cc", "-O2", "-std=gnu89", "-o", str(cc), str(ROOT / "tests/ccdcw.c")], check=True)
        subprocess.run(["cc", "-O2", "-std=gnu89", "-w", "-o", str(nc), str(ROOT / "tests/ncserver.c"),
                        str(ROOT / "src/des.c"), str(ROOT / "src/msg-newcamd.c"), str(ROOT / "src/md5.c")], check=True)
        ports = set()
        while len(ports) < 5: ports.add(free_port())
        http, cccam, newcamd, card1, card2 = ports
        cfg = tmp / "mcs.cfg"
        cfg.write_text(
            f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\n"
            f"TELNET PORT: 0\nCCCAM PORT: {cccam}\nF: traceuser tracepass\n"
            f"N: 127.0.0.1 {card1} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n"
            f"N: 127.0.0.1 {card2} srv2 pass2 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n"
            f"\n[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n"
            f"DCW TIMEOUT: 3000\nSERVER TIMEOUT: 4000\nDCW HALFNULLED: YES\nDCW SWAP: YES\n"
            f"SERVER INTERVAL: 200\nSERVER MAX: 2\n"
            f"USER: traceuser tracepass\n", encoding="utf-8")
        children = []
        try:
            with (tmp / "peer1.log").open("w+") as p1log, (tmp / "peer2.log").open("w+") as p2log, \
                 (tmp / "server.log").open("w+") as slog:
                env1 = dict(os.environ, NS_DELAYS="2500")   # slow: loses the race
                env2 = dict(os.environ, NS_DELAYS="60")     # fast: commits first
                for e in (env1, env2):
                    for k in ("NS_HALF_CW", "NS_BAD_CHECKSUM", "NS_NEGATIVE", "NS_OPPOSITE_HALF", "NS_NEGATIVE_FIRST", "NS_CW_SEQUENCE"):
                        e.pop(k, None)
                if MODE == "mismatch":
                    env2["NS_CW_SEQUENCE"] = "010203060405060F070809180A0B0C21"
                peer1 = subprocess.Popen([str(nc), str(card1), "0102030405060708091011121314", "1884", "000000"],
                                         cwd=tmp, env=env1, stdout=p1log, stderr=subprocess.STDOUT)
                peer2 = subprocess.Popen([str(nc), str(card2), "0102030405060708091011121314", "1884", "000000"],
                                         cwd=tmp, env=env2, stdout=p2log, stderr=subprocess.STDOUT)
                children += [peer1, peer2]
                time.sleep(0.3)
                server = subprocess.Popen([str(BINARY), "-v", "-C", str(cfg)], cwd=tmp,
                                          stdout=slog, stderr=subprocess.STDOUT)
                children.append(server)
                wait_until(lambda: connected(cccam), children)
                def logged(name):
                    return "login from user" in (tmp / name).read_text(errors="replace")
                wait_until(lambda: logged("peer1.log") and logged("peer2.log"), children)
                auth = "Basic " + base64.b64encode(b"test:isolated").decode()
                url = f"http://127.0.0.1:{http}/user-trace"
                def request(data=None):
                    r = urllib.request.Request(url, data=data, headers={"Authorization": auth})
                    with urllib.request.urlopen(r, timeout=5) as resp:
                        return resp.read().decode("utf-8")
                page = request()
                token = re.search("name='csrf' value='([a-f0-9]{64})'", page)
                client = re.search("<option value='(\\d+)'[^>]*>traceuser", page)
                assert token and client, "trace form not found"
                request(urllib.parse.urlencode({"csrf": token.group(1), "action": "start",
                        "client": client.group(1), "caid": "1884", "provid": "000000", "sid": "1136"}).encode())
                result = subprocess.run([str(cc), "127.0.0.1", str(cccam), "traceuser", "tracepass",
                                         "1884", "1136", "0x80"], capture_output=True, text=True, timeout=25)
                print("client:", result.stdout.strip().splitlines()[:2])
                # give the slow server's late CW time to reach the commit path
                time.sleep(4)
                page = request()
                assert "نظرة عامة" in page, "overview section missing"
                assert "CAIDs مراقبة: 1" in page, "DoubleCheck not armed from profile CAIDs"
                want = "✅ تطابق مصدرين" if MODE == "agree" else "⚠️ عدم تطابق المصادر"
                if want not in page:
                    m = re.search(r"التحقق المزدوج[^<]*", page)
                    print("DOUBLECHECK LINE:", m.group(0) if m else "not found")
                    print("server log tail:")
                    print("\n".join((tmp / "server.log").read_text(errors="replace").splitlines()[-15:]))
                    raise AssertionError(f"expected decision {want!r} on trace page (MODE={MODE})")
                print(f"OK MODE={MODE}: decision visible on /user-trace: {want}")
        finally:
            for c in children: stop(c)
            if os.environ.get("MCS_DUMP_SERVERLOG"):
                try:
                    print("---- server.log ----")
                    print((tmp / "server.log").read_text(errors="replace"))
                except OSError:
                    pass

if __name__ == "__main__":
    main()
