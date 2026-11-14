#!/usr/bin/env python3
"""Live FreeCCcam server proof: a CCcam-protocol client served through
the FREECCCAM listener, with one synthetic Newcamd card behind it.

FreeCCcam had no network harness at all (only config-parse mentions),
so its accept/recv/delivery threads never ran under test. This starts
the real server binary with FREECCCAM PORT/USERNAME/PASSWORD/PROFILES
configured, connects tests/ccdcw.c as a FreeCCcam client, and requires
a DCW answer sourced from the synthetic card. Under the TSan session
(tests/tsan-r22-session.sh) sanitizer reports are captured via
TSAN_OPTIONS log_path by the caller.

Usage: python3 tests/freecccam-live.py [path-to-multics-binary]
"""
import base64, os, re, socket, subprocess, sys, tempfile, time
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
        while len(ports) < 5: ports.add(free_port())
        http, freeport, newcamd, card, _unused = ports
        cfg = tmp / "mcs.cfg"
        cfg.write_text(
            f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\n"
            f"TELNET PORT: 0\n"
            f"FREECCCAM PORT: {freeport}\nFREECCCAM USERNAME: freeuser\n"
            f"FREECCCAM PASSWORD: freepass\nFREECCCAM MAXUSERS: 5\nFREECCCAM PROFILES: {newcamd}\n"
            f"N: 127.0.0.1 {card} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n"
            f"\n[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {newcamd}\n"
            f"DCW TIMEOUT: 3000\nDCW HALFNULLED: YES\nDCW SWAP: YES\n"
            f"USER: u1 p1\n", encoding="utf-8")
        children = []
        try:
            with (tmp / "peer.log").open("w+") as plog, (tmp / "server.log").open("w+") as slog:
                env = dict(os.environ, NS_DELAYS="60")
                peer = subprocess.Popen([str(nc), str(card), "0102030405060708091011121314", "1884", "000000"],
                                        cwd=tmp, env=env, stdout=plog, stderr=subprocess.STDOUT)
                children.append(peer)
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
                client_env = dict(os.environ, CCDCW_CLIINFO97="1", CCDCW_CARDID="100")
                result = subprocess.run([str(cc), "127.0.0.1", str(freeport), "freeuser", "freepass",
                                         "1884", "1136", "0x80"], capture_output=True, text=True,
                                        timeout=25, env=client_env)
                print("client:", result.stdout.strip().splitlines()[:2])
                assert "ECM1 tag=80 DCW" in result.stdout, result.stdout
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
    main()
