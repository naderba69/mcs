#!/usr/bin/env python3
"""autelnet.py -- TASK R3 (D57) live-rig helper: one telnet login attempt,
timed. Prints RESULT ok|denied|closed ELAPSED <ms> and nothing else on stdout.

  python3 autelnet.py PORT USER PASS

- ok     : the login passed (the 'type help' banner arrived)
- denied : the server answered the stock wrong-user/wrong-password line
- closed : the connection died without a failure line (allowlist pre-auth close)
"""
import socket, sys, time

def main():
    port = int(sys.argv[1]); user = sys.argv[2]; pw = sys.argv[3]
    t0 = time.time()
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=10)
    except OSError:
        print("RESULT closed ELAPSED %d" % int((time.time()-t0)*1000)); return
    def rd(wait=3.0):
        s.settimeout(wait)
        try:
            return s.recv(4096).decode(errors="replace")
        except (OSError, socket.timeout):
            return ""
    banner = rd()
    if not banner:
        print("RESULT closed ELAPSED %d" % int((time.time()-t0)*1000)); return
    s.sendall((user+"\r\n").encode())
    rd(0.6)
    s.sendall((pw+"\r\n").encode())
    out = rd(3.0) + rd(1.0)
    ms = int((time.time()-t0)*1000)
    if "help" in out or "[command]" in out:
        print("RESULT ok ELAPSED %d" % ms)
    elif "wrong" in out or "bye" in out:
        print("RESULT denied ELAPSED %d" % ms)
    else:
        print("RESULT closed ELAPSED %d" % ms)
    s.close()

if __name__ == "__main__":
    main()
