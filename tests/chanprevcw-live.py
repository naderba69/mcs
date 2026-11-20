#!/usr/bin/env python3
"""Live proof that the band-115 channel memory is fed by a LOCAL delivery.

Why this test exists: for the whole life of the feature its counters printed
"Wrote=0" on /user-trace, and that zero was used as evidence about cache
behaviour. It was not evidence at all -- the state lived in `static` objects in
a header included by several translation units, so the page was reading a copy
nothing ever writes (band 119 notes). A counter that cannot move is worse than
no counter: it answers the wrong question with authority.

So this test drives ONE real client ECM through ONE fake card server, which is a
delivery that passes no cache gate at all, and demands that the page show the
write. That is the exact edge band 119 added, and the exact number that was
untrustworthy before it.

Usage: python3 tests/chanprevcw-live.py [path-to-multics-binary]
"""
import base64, os, re, socket, subprocess, sys, tempfile, time
import urllib.parse, urllib.request
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
    with tempfile.TemporaryDirectory(prefix="mcs-chanprevcw-") as folder:
        tmp = Path(folder)
        cc, nc = tmp / "ccdcw", tmp / "ncserver"
        subprocess.run(["cc", "-O2", "-std=gnu89", "-o", str(cc), str(ROOT / "tests/ccdcw.c")], check=True)
        subprocess.run(["cc", "-O2", "-std=gnu89", "-w", "-o", str(nc), str(ROOT / "tests/ncserver.c"),
                        str(ROOT / "src/des.c"), str(ROOT / "src/msg-newcamd.c"), str(ROOT / "src/md5.c")], check=True)
        ports = set()
        while len(ports) < 3: ports.add(free_port())
        http, cccam, card1 = list(ports)   # card1 doubles as the profile's
        # listening port: the fake card server takes it, exactly like
        # doublecheck-live.py, and the profile points at it.
        cfg = tmp / "mcs.cfg"
        cfg.write_text(
            f"HTTP PORT: {http}\nHTTP USER: test\nHTTP PASS: isolated\n"
            f"TELNET PORT: 0\nCCCAM PORT: {cccam}\nF: traceuser tracepass\n"
            f"N: 127.0.0.1 {card1} srv1 pass1 01 02 03 04 05 06 07 08 09 10 11 12 13 14\n"
            f"\n[ isolated ]\nCAID: 1884\nPROVIDERS: 0\nPORT: {card1}\n"
            f"DCW TIMEOUT: 3000\nSERVER TIMEOUT: 4000\nDCW HALFNULLED: YES\nDCW SWAP: YES\n"
            f"SERVER INTERVAL: 200\nSERVER MAX: 1\n"
            f"USER: traceuser tracepass\n", encoding="utf-8")
        # CACHE CYCLE MEMORY is a GLOBAL keyword (config.c ~4890, "MEMORY" branch
        # under "CACHE CYCLE"), written in the field file at line 259 as
        #   CACHE CYCLE MEMORY: ON TTL: 45000
        # so it must sit above the profile block; the one-line TTL continuation is
        # what the parser accepts. Appending it inside [ isolated ] would parse as
        # nothing and leave the feature off -- the test would then "prove" the
        # wiring dead when only its config line was misplaced.
        cfg.write_text(cfg.read_text().replace(
            "\n[ isolated ]\n", "\nCACHE CYCLE MEMORY: ON TTL: 30000\n\n[ isolated ]\n"),
            encoding="utf-8")
        children = []
        try:
            with (tmp / "peer1.log").open("w+") as p1log, (tmp / "server.log").open("w+") as slog:
                env1 = dict(os.environ, NS_DELAYS="60")
                for k in ("NS_HALF_CW", "NS_BAD_CHECKSUM", "NS_NEGATIVE", "NS_OPPOSITE_HALF",
                          "NS_NEGATIVE_FIRST", "NS_CW_SEQUENCE"):
                    env1.pop(k, None)
                peer1 = subprocess.Popen([str(nc), str(card1), "0102030405060708091011121314", "1884", "000000"],
                                         cwd=tmp, env=env1, stdout=p1log, stderr=subprocess.STDOUT)
                children.append(peer1)
                time.sleep(0.3)
                server = subprocess.Popen([str(BINARY), "-v", "-C", str(cfg)], cwd=tmp,
                                          stdout=slog, stderr=subprocess.STDOUT)
                children.append(server)
                wait_until(lambda: connected(cccam), children)
                wait_until(lambda: "login from user" in (tmp / "peer1.log").read_text(errors="replace"), children)
                auth = "Basic " + base64.b64encode(b"test:isolated").decode()
                url = f"http://127.0.0.1:{http}/user-trace"
                def request(data=None):
                    r = urllib.request.Request(url, data=data, headers={"Authorization": auth})
                    with urllib.request.urlopen(r, timeout=5) as resp:
                        # errors="replace": the page mixes Arabic labels with the
                        # profile's own channel names; a stray byte must not fail
                        # a test whose only job is to read two numbers off it.
                        return resp.read().decode("utf-8", "replace")
                def counters(page):
                    m = re.search(r"كُتب (\d+) \(منه من التسليم المحلي (\d+)\)", page)
                    if m: return int(m.group(1)), int(m.group(2))
                    # Pre-band-119 print ("كُتب 0 · قُرئت ..."), no local
                    # split. Reading the older shape too is the point: the
                    # same probe then FAILS on the pre-fix binary for the
                    # semantic reason -- an ECM served, Wrote still 0 -- not
                    # over a changed label. That before/after is the proof.
                    m = re.search(r"كُتب (\d+) ·", page)
                    return (int(m.group(1)), 0) if m else None
                page = request()
                pair = counters(page)
                assert pair is not None, "the channel-memory counter line is missing from /user-trace"
                assert pair == (0, 0), f"memory written before any delivery: {pair}"
                print(f"before the delivery: Wrote={pair[0]} local={pair[1]} (as expected: nothing served yet)")

                # Band 121 rides along here because this is the harness that boots
                # a real binary and reads the page: the identity line must RENDER,
                # not merely exist in the source, and its band must be the header's.
                bi = re.search(r"بِناء البند\s*(?:<b>)?(\d+)", page)   # <b> wraps the number
                hdr = (ROOT / "src/build-identity.h").read_text(encoding="utf-8")
                want = int(re.search(r"#define\s+MCS_BUILD_BAND\s+(\d+)", hdr).group(1))
                assert bi and int(bi.group(1)) == want, (
                    f"identity line on /user-trace is {bi and bi.group(1)!r}, header says "
                    f"{want} -- the page must not be able to claim a stale build")
                n_id = page.count("هوية هذا الثنائي")
                assert n_id == 1, f"{n_id} identity lines on one page"
                print(f"identity line rendered: band {bi.group(1)}, exactly one emitter")

                token = re.search("name='csrf' value='([a-f0-9]{64})'", page)
                client = re.search("<option value='(\d+)'[^>]*>traceuser", page)
                assert token and client, "trace form not found"
                request(urllib.parse.urlencode({"csrf": token.group(1), "action": "start",
                        "client": client.group(1), "caid": "1884", "provid": "000000", "sid": "1136"}).encode())
                result = subprocess.run([str(cc), "127.0.0.1", str(cccam), "traceuser", "tracepass",
                                         "1884", "1136", "0x80"], capture_output=True, text=True, timeout=25)
                print("client:", result.stdout.strip().splitlines()[:2])
                deadline = time.monotonic() + 8
                pair = None
                while time.monotonic() < deadline:
                    pair = counters(request())
                    if pair and pair[0] >= 1 and pair[1] >= 1: break
                    time.sleep(0.4)
                if not (pair and pair[0] >= 1 and pair[1] >= 1):
                    print("COUNTER LINE:", pair)
                    print("\n".join((tmp / "server.log").read_text(errors="replace").splitlines()[-20:]))
                    raise AssertionError("a committed local delivery did not reach the channel memory "
                                         "(Wrote/local still zero) -- band 119 wiring is not live")
                print(f"OK: one served ECM == Wrote={pair[0]} of which local-delivery={pair[1]}; "
                      "the counter moves, so the page can be trusted as evidence again")
        finally:
            for c in children: stop(c)
            if os.environ.get("MCS_DUMP_SERVERLOG"):
                print("---- server.log ----")
                print((tmp / "server.log").read_text(errors="replace"))

if __name__ == "__main__":
    main()
