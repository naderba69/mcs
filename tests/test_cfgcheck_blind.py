#!/usr/bin/env python3
"""Unit checks for cfgcheck's conservative blind-configuration warning."""
import os
import sys
import tempfile

sys.dont_write_bytecode = True
from cfgcheck import has_outbound_line_definition, parse, valid_timeout_value


def check(text, expected, label):
    fd, path = tempfile.mkstemp(prefix="cfgcheck-blind-", text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            out.write(text)
        glob, profiles = parse(path)
        actual = has_outbound_line_definition(glob, profiles)
        if actual != expected:
            raise AssertionError("%s: expected %s, got %s" % (label, expected, actual))
    finally:
        os.unlink(path)


check("# N: comment is not a server\nF: local-account\n", False,
      "comments and local CCcam accounts do not anchor a line")
records = {
    "N": "host.example 1234 user pass 0102030405060708090a0b0c0d0e",
    "C": "host.example 1234 user pass",
    "L": "host.example 1234 user pass",
    "R": "host.example 1234 1814 010203",
}
for protocol, record in records.items():
    check("%s: %s\n" % (protocol, record), True,
          "global %s line definition" % protocol)
    check("[profile]\nCAID: 1814\n%s: %s\n" % (protocol, record),
          True, "profile-contained %s line definition" % protocol)
    check("%s: host not-a-port user pass\n" % protocol, False,
          "malformed %s line does not anchor" % protocol)

assert valid_timeout_value("CACHE TIMEOUT", 0)
assert valid_timeout_value("CACHE TIMEOUT", 5000)
assert not valid_timeout_value("CACHE TIMEOUT", 5001)
assert valid_timeout_value("SERVER TIMEOUT", 300)
assert valid_timeout_value("SERVER TIMEOUT", 10000)
assert not valid_timeout_value("SERVER TIMEOUT", 299)
assert not valid_timeout_value("SERVER TIMEOUT", 10001)
print("PASS: cfgcheck timeouts follow their distinct parser ranges")

print("PASS: cfgcheck blind-configuration detection (valid N/C/L/R only)")
