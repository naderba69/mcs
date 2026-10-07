#!/usr/bin/env python3
"""cedcw.py -- fetch `dcwstats` from the rig's telnet port and print it.

usage: cedcw.py <host> <port> <user> <pass>
Prints one `<name> <count>` line per counter; the `ce` target greps it.
"""
import socket
import sys
import time

def main():
    host, port, user, pw = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    s = socket.create_connection((host, port), timeout=6)

    def rd():
        time.sleep(0.5)
        try:
            return s.recv(65536).decode(errors="replace")
        except OSError:
            return ""

    rd()                                    # banner + Login:
    s.sendall((user + "\r\n").encode())     # login is CRLF lines
    rd()                                    # Password:
    s.sendall((pw + "\r\n").encode())       # prompt
    rd()
    s.sendall(b"dcwstats\r\n")
    out = rd()
    s.close()
    for line in out.splitlines():
        line = line.strip()
        if line and not line.startswith("["):
            print(line)

if __name__ == "__main__":
    main()
