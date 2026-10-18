#!/usr/bin/env python3
# TASK R48 (D111): a tiny telnet client for the live checks of r48-scope.sh.
# Usage: r48-tn.py PORT USER PASS 'command' ['command' ...]
# Logs in, sends each command on its own line, prints each reply. Stdlib only.
import socket
import sys


def read_reply(sock):
    sock.settimeout(0.6)
    data = b""
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data.decode("latin-1")


def main():
    port, user, password = int(sys.argv[1]), sys.argv[2], sys.argv[3]
    commands = sys.argv[4:]
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    read_reply(sock)                                   # "Login: "
    sock.sendall(user.encode() + b"\r\n")
    read_reply(sock)                                   # "Password: "
    sock.sendall(password.encode() + b"\r\n")
    read_reply(sock)                                   # banner
    for cmd in commands:
        sock.sendall(cmd.encode() + b"\r\n")
        sys.stdout.write(read_reply(sock))
    sock.sendall(b"exit\r\n")
    read_reply(sock)
    sock.close()


if __name__ == "__main__":
    main()
