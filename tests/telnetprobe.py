#!/usr/bin/env python3
"""Probe for the telnet console -- TASK R14 (D68).

The `tl2` target needs three things the shell cannot express cleanly (the
earlier attempt put a heredoc inside a make recipe and make refused to parse
it): hold a socket that says nothing and measure how the server ends it, open
several sessions at once and read what the door answers, and log in properly.
Each mode prints ONE line; the recipe decides what it means.

    telnetprobe.py idle  <port> [wait_s]  -> "closed after N.Ns" | "still open after Ns"
    telnetprobe.py cap   <port> <hold>    -> the sentence the extra session gets
    telnetprobe.py login <port> <user> <pass> -> "console" | "silent:<first bytes>"

Exit status is always 0: this is a measuring instrument, the assertions live
in the recipe (same split as cedcw.py and autelnet.py).
"""
import socket
import sys
import time


def idle(port, wait_s):
    s = socket.create_connection(("127.0.0.1", port), 5)
    s.recv(200)                      # the welcome prompt: the session is live
    t0 = time.time()
    s.settimeout(wait_s)
    try:
        while True:
            d = s.recv(200)
            if not d:
                print("closed after %.1fs" % (time.time() - t0))
                break
    except socket.timeout:
        print("still open after %ds" % wait_s)
    finally:
        s.close()


def cap(port, hold):
    holds = []
    for _ in range(hold):
        s = socket.create_connection(("127.0.0.1", port), 5)
        s.recv(200)
        holds.append(s)
    time.sleep(0.5)                  # let the accept loop see them all
    extra = socket.create_connection(("127.0.0.1", port), 5)
    extra.settimeout(5)
    try:
        d = extra.recv(200)
    except socket.timeout:
        d = b"<timeout>"
    print(d.decode("latin1").strip())
    extra.close()
    for s in holds:
        s.close()


def login(port, user, password):
    s = socket.create_connection(("127.0.0.1", port), 5)
    s.recv(200)
    s.sendall((user + "\r\n").encode())
    s.recv(200)
    s.sendall((password + "\r\n").encode())
    s.settimeout(5)
    d = s.recv(400)
    s.close()
    print("console" if b"help" in d else "silent:%r" % d[:40])


def main():
    if len(sys.argv) < 3:
        print("usage: telnetprobe.py idle|cap|login <port> [...]")
        return 0
    mode, port = sys.argv[1], int(sys.argv[2])
    if mode == "idle":
        idle(port, int(sys.argv[3]) if len(sys.argv) > 3 else 12)
    elif mode == "cap":
        cap(port, int(sys.argv[3]) if len(sys.argv) > 3 else 3)
    elif mode == "login":
        login(port, sys.argv[3], sys.argv[4])
    else:
        print("unknown mode")
    return 0


if __name__ == "__main__":
    sys.exit(main())
