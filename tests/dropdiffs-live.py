#!/usr/bin/env python3
"""Live CACHE DROPDIFFS proof (band 32).

Two synthetic cards serve the same channel: the fast card's CW is
delivered (and stored as known-good for that ECM), the slow card's
different CW arrives afterwards. With CACHE DROPDIFFS: ON the late
differing candidate must be refused with a DropDiffs event on
/user-trace; with OFF (stock) it passes validation as a late
candidate. The client receives the fast card's DCW in both runs.

Usage: python3 tests/dropdiffs-live.py [path-to-multics-binary]
"""
import base64, os, re, socket, subprocess, sys, tempfile, time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "bin/multics.x64"

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
    with tempfile.TemporaryDirectory(prefix="mcs-freecccam-") as folder:
        tmp = Path(folder)
        cc, nc = tmp / "ccdcw", tmp / "ncserver"
        subprocess.run(["cc", "-O2", "-std=gnu89", "-o", str(cc), str(ROOT / "tests/ccdcw.c")], check=True)
        subprocess.run(["cc", "-O2", "-std=gnu89", "-w", "-o", str(nc), str(ROOT / "tests/ncserver.c"),
                        str(ROOT / "src/des.c"), str(ROOT / "src/msg-newcamd.c"), str(ROOT / "src/md5.c")], check=True)
        ports = set()
        while len(ports) < 6: ports.add(free_port())
        http, freeport, newcamd, card, card2, ccport = ports
        cfg = tmp / "mcs.cfg"
        gate = "CACHE DROPDIFFS: ON\n"  # band 33: global ON in BOTH runs; the per-profile line below must win (OFF run proves the override)
        cfg.write_text(
            gate +            f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\n"
            f"TELNET PORT: 0\n"
            f"CCCAM PORT: {ccport}\nF: ccuser ccpass\n"
            f"FREECCCAM PORT: {freeport}\nFREECCCAM USERNAME: freeuser\n"
            f"FREECCCAM PASSWORD: freepass\nFREECCCAM MAXUSERS: 5\nFREECCCAM PROFILES: {newcamd}\n"
            f"N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n"
            f"N: 127.0.0.1 {card2} srv2 pass2 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n"
            f"\n[ isolated ]\nCAID: 1884\n"
            + ("DROPDIFFS: ON\n" if os.environ.get("DD_MODE") == "on" else "DROPDIFFS: OFF\n")
            + f"PROVIDERS: 0\nPORT: {newcamd}\n"
            f"DCW TIMEOUT: 3000\nSERVER INTERVAL: 200\nSERVER MAX: 2\n"
            f"DCW HALFNULLED: YES\nDCW SWAP: YES\n"
            f"USER: u1 p1\n", encoding="utf-8")
        children = []
        try:
            with (tmp / "peer.log").open("w+") as plog, (tmp / "server.log").open("w+") as slog:
                env = dict(os.environ, NS_DELAYS="1500",
                           NS_CW_SEQUENCE="11223366445566FF771122AA21324396")
                peer = subprocess.Popen([str(nc), str(card), "0102030405060708091011121314", "1884", "000000"],
                                        cwd=tmp, env=env, stdout=plog, stderr=subprocess.STDOUT)
                children.append(peer)
                env2 = dict(os.environ, NS_DELAYS="60")
                peer2 = subprocess.Popen([str(nc), str(card2), "0102030405060708091011121314", "1884", "000000"],
                                         cwd=tmp, env=env2, stdout=plog, stderr=subprocess.STDOUT)
                children.append(peer2)
                time.sleep(0.3)
                server = subprocess.Popen([str(BINARY), "-v", "-C", str(cfg)], cwd=tmp,
                                          stdout=slog, stderr=subprocess.STDOUT)
                children.append(server)
                wait_until(lambda: connected(freeport), children)
                def peer_ready():
                    return "login from user" in (tmp / "peer.log").read_text(errors="replace")
                wait_until(peer_ready, children)
                # FreeCCcam's handshake wants the 93-byte CLI_INFO form
                # and resolves the profile by card id; profile ids start
                # at 0x64 (config.c), and this is the first profile.
                # Start an operator trace in both modes (the DD_MODE decides the gate)
                if True:
                    # Start an operator trace for the FreeCCcam client
                    # exactly as the page's form does, so the rejection
                    # must become visible on /user-trace itself.
                    auth = "Basic " + base64.b64encode(b"test:isolated").decode()
                    def page():
                        req = urllib.request.Request(f"http://127.0.0.1:{http}/user-trace",
                                                     headers={"Authorization": auth})
                        with urllib.request.urlopen(req, timeout=5) as resp:
                            return resp.read().decode("utf-8", "replace")
                    form = page()
                    csrf = re.search(r"name='csrf' value='([0-9a-f]+)'", form).group(1)
                    # slots are listed head-first; the connect path also takes the
                    # head slot first, so the first option is our client
                    cid = re.search(r"<option value='(\d+)' data-caid", form).group(1)
                    body = urllib.parse.urlencode(
                        {"csrf": csrf, "action": "start", "client": cid, "mode": "all"}).encode()
                    req = urllib.request.Request(f"http://127.0.0.1:{http}/user-trace", data=body,
                                                 headers={"Authorization": auth,
                                                          "Content-Type": "application/x-www-form-urlencoded"})
                    with urllib.request.urlopen(req, timeout=5) as resp:
                        assert resp.status == 200
                client_env = dict(os.environ, CCDCW_CLIINFO97="1", CCDCW_CARDID="100")
                result = subprocess.run([str(cc), "127.0.0.1", str(freeport), "freeuser", "freepass",
                                         "1884", "1136", "0x80"], capture_output=True, text=True,
                                        timeout=25, env=client_env)
                print("client:", result.stdout.strip().splitlines()[:2])
                time.sleep(2.5)  # let the slow card's differing CW arrive
                if os.environ.get("DD_MODE") == "on":
                    # The client receives the fast card's DCW in both
                    # modes; only the late differing candidate's fate
                    # differs, and it must be visible on /user-trace.
                    assert "ECM1 tag=80 DCW" in result.stdout, result.stdout
                    auth = "Basic " + base64.b64encode(b"test:isolated").decode()
                    req = urllib.request.Request(f"http://127.0.0.1:{http}/user-trace",
                                                 headers={"Authorization": auth})
                    with urllib.request.urlopen(req, timeout=5) as resp:
                        page = resp.read().decode("utf-8", "replace")
                    assert "DropDiffs" in page, "DropDiffs rejection missing from /user-trace"
                    print("OK DROPDIFFS refused the late differing CW (visible on /user-trace)")
                    return
                assert "ECM1 tag=80 DCW" in result.stdout, result.stdout
                auth = "Basic " + base64.b64encode(b"test:isolated").decode()
                req = urllib.request.Request(f"http://127.0.0.1:{http}/user-trace",
                                             headers={"Authorization": auth})
                with urllib.request.urlopen(req, timeout=5) as resp:
                    page = resp.read().decode("utf-8", "replace")
                assert "DropDiffs" not in page, "gate fired while OFF"
                print("OK DROPDIFFS off: late candidate not refused (stock)")
                # the operator page must still answer after the session
                auth = "Basic " + base64.b64encode(b"test:isolated").decode()
                req = urllib.request.Request(f"http://127.0.0.1:{http}/user-trace",
                                             headers={"Authorization": auth})
                with urllib.request.urlopen(req, timeout=5) as resp:
                    assert resp.status == 200
                print("OK FreeCCcam client received DCW from the synthetic card")
        finally:
            for c in children: stop(c)
            if os.environ.get("MCS_DUMP_SERVERLOG"):
                try:
                    print("---- server.log ----")
                    print((tmp / "server.log").read_text(errors="replace"))
                except OSError:
                    pass

if __name__ == "__main__":
    if os.environ.get("DD_MODE"):
        main()
    else:
        for mode in ("off", "on"):
            env = dict(os.environ, DD_MODE=mode)
            r = subprocess.run([sys.executable, __file__] + sys.argv[1:], env=env)
            assert r.returncode == 0, mode
        print("PASS dropdiffs-live (off stock, on refuses late differing CW)")
