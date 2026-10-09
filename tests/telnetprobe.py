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
    telnetprobe.py update-pass <port> <telnet-user> <telnet-pass>
        -> "rejected; session-alive" when an overlong CCCAM password is refused

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


def recv_until(sock, marker):
    data = b""
    sock.settimeout(5)
    while marker not in data:
        chunk = sock.recv(1024)
        if not chunk:
            break
        data += chunk
    return data


def update_pass(port, user, password):
    sock = socket.create_connection(("127.0.0.1", port), 5)
    try:
        recv_until(sock, b"Login:")
        sock.sendall((user + "\r\n").encode("ascii"))
        recv_until(sock, b"Password:")
        sock.sendall((password + "\r\n").encode("ascii"))
        recv_until(sock, b"[command]: ")

        too_long = "A" * 80
        command = "UPDATE CCCAM 1 m5user {} * 1\r\n".format(too_long)
        sock.sendall(command.encode("ascii"))
        response = recv_until(sock, b"[command]: ")

        sock.sendall(b"STAT\r\n")
        status = recv_until(sock, b"[command]: ")
        if (b"CCcam password too long (max 63 bytes), unchanged." in response
                and b"Total Profiles:" in status
                and b"Total CCcam Servers:" in status):
            print("rejected; session-alive")
        else:
            print("unexpected: response={!r} status={!r}".format(response[-180:], status[-180:]))
    except (OSError, socket.timeout) as error:
        print("probe-error: {}".format(error))
    finally:
        sock.close()


def main():
    if len(sys.argv) < 3:
        print("usage: telnetprobe.py idle|cap|login|update-pass <port> [...]")
        return 0
    mode, port = sys.argv[1], int(sys.argv[2])
    if mode == "idle":
        idle(port, int(sys.argv[3]) if len(sys.argv) > 3 else 12)
    elif mode == "cap":
        cap(port, int(sys.argv[3]) if len(sys.argv) > 3 else 3)
    elif mode == "login":
        login(port, sys.argv[3], sys.argv[4])
    elif mode == "update-pass":
        update_pass(port, sys.argv[3], sys.argv[4])
    else:
        print("unknown mode")
    return 0


if __name__ == "__main__":
    sys.exit(main())
