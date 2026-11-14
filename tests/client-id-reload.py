#!/usr/bin/env python3
"""Live client-id generation proof across a config reload.

Gen 1: FREECCCAM MAXUSERS 5 slots plus one regular CCcam client.
Reload (in place, same inode -- sed -i would break inotify) raises
MAXUSERS to 7, which swaps the FreeCCcam slot list. Live CCcam
clients keep their objects and ids across a reload, while incoming
slots used to be renumbered from 1 by the fresh config: a kept
client inside the new slot range collided with a slot, and the
id-matched user trace could bind to the wrong client.

Asserts on the /user-trace picker, before and after: every client
id is unique, and the kept CCcam client keeps its id.

Usage: python3 tests/client-id-reload.py [path-to-multics-binary]
"""
import base64, os, re, socket, subprocess, sys, tempfile, time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "bin/multics.x64"

def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0)); return s.getsockname()[1]

def main():
    assert BINARY.is_file(), BINARY
    with tempfile.TemporaryDirectory(prefix="mcs-idreload-") as folder:
        tmp = Path(folder)
        http, freeport, ccport = free_port(), free_port(), free_port()
        while len({http, freeport, ccport}) < 3:
            http, freeport, ccport = free_port(), free_port(), free_port()
        cfg = tmp / "mcs.cfg"
        def render(maxusers):
            return (f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\n"
                    f"TELNET PORT: 0\nCCCAM PORT: {ccport}\nF: ccuser ccpass\n"
                    f"FREECCCAM PORT: {freeport}\nFREECCCAM USERNAME: freeuser\n"
                    f"FREECCCAM PASSWORD: freepass\nFREECCCAM MAXUSERS: {maxusers}\n"
                    f"FREECCCAM PROFILES: 15001\n\n[ myprofile ]\nCAID: 1884\n"
                    f"PROVIDERS: 0\nPORT: 15001\nUSER: u1 p1\n")
        cfg.write_text(render(5), encoding="utf-8")
        auth = "Basic " + base64.b64encode(b"test:isolated").decode()
        def picker():
            req = urllib.request.Request(f"http://127.0.0.1:{http}/user-trace",
                                         headers={"Authorization": auth})
            with urllib.request.urlopen(req, timeout=5) as resp:
                page = resp.read().decode("utf-8", "replace")
            opts = re.findall(r"<option value='(\d+)' data-caid[^>]*>([^<]*)</option>", page)
            return [(int(i), name) for i, name in opts]
        server = subprocess.Popen([str(BINARY), "-v", "-C", str(cfg)], cwd=tmp,
                                  stdout=open(tmp / "server.log", "w"),
                                  stderr=subprocess.STDOUT)
        try:
            end = time.monotonic() + 20
            before = []
            while time.monotonic() < end:
                try: before = picker()
                except Exception: pass
                if len(before) >= 6: break
                time.sleep(0.2)
            assert len(before) == 6, before
            ids = [i for i, _ in before]
            assert len(ids) == len(set(ids)), f"gen1 duplicate ids: {ids}"
            cc_before = [i for i, name in before if "ccuser" in name]
            assert len(cc_before) == 1, before

            # in-place rewrite (same inode) -> inotify reload
            with open(cfg, "r+") as f:
                f.seek(0); f.write(render(7)); f.truncate()

            after = []
            end = time.monotonic() + 30
            while time.monotonic() < end:
                try: after = picker()
                except Exception: pass
                if len(after) >= 8: break
                time.sleep(0.5)
            assert len(after) == 8, after
            ids = [i for i, _ in after]
            assert len(ids) == len(set(ids)), f"gen2 duplicate ids after slot swap: {ids}"
            cc_after = [i for i, name in after if "ccuser" in name]
            assert cc_after == cc_before, (cc_before, cc_after)
            print(f"PASS client-id-reload (gen1 ids {sorted(i for i,_ in before)}, "
                  f"gen2 unique after MAXUSERS 5->7 swap, ccuser kept id {cc_before[0]})")
        finally:
            server.terminate()
            try: server.wait(timeout=5)
            except subprocess.TimeoutExpired: server.kill()

if __name__ == "__main__":
    main()
