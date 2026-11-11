#!/usr/bin/env python3
"""Read a cache peer's sent-request counter through the existing web UI.

The /json readout intentionally reports received requests as `ecm`; the cache
page's existing row refresh exposes sent requests as <c6> (c7 is sent replies).
This helper follows that UI path and keeps the live peerrep test from inferring
sends from logs.
"""

import base64
import sys
from html.parser import HTMLParser
from urllib.error import URLError
from urllib.request import Request, urlopen
from xml.etree import ElementTree


class CacheRows(HTMLParser):
    def __init__(self):
        super().__init__()
        self.current_id = None
        self.current_text = []
        self.current_depth = 0
        self.rows = []

    def handle_starttag(self, tag, attrs):
        if tag.lower() == "tr":
            if self.current_id is not None:
                self.current_depth += 1
                return
            attrs = dict(attrs)
            row_id = attrs.get("id", "")
            if row_id.startswith("Row"):
                self.current_id = row_id[3:]
                self.current_text = []
                self.current_depth = 1

    def handle_data(self, data):
        if self.current_id is not None:
            self.current_text.append(data)

    def handle_endtag(self, tag):
        if tag.lower() == "tr" and self.current_id is not None and self.current_depth:
            self.current_depth -= 1
            if self.current_depth == 0:
                self.rows.append((self.current_id, "".join(self.current_text)))
                self.current_id = None
                self.current_text = []


def fetch(base, path):
    token = base64.b64encode(b"a:a").decode("ascii")
    request = Request(base + path, headers={"Authorization": "Basic " + token})
    with urlopen(request, timeout=3) as response:
        return response.read()


def main():
    if len(sys.argv) != 3:
        return 2
    try:
        http_port = int(sys.argv[1])
        peer_port = int(sys.argv[2])
        base = "http://127.0.0.1:%d" % http_port
        page = fetch(base, "/cache").decode("utf-8", "replace")
        rows = CacheRows()
        rows.feed(page)
        needle = ":%d" % peer_port
        peer_id = next((row_id for row_id, text in rows.rows if needle in text), None)
        if peer_id is None:
            print(-1)
            return 0
        xml = fetch(base, "/cache?peerid=%s" % peer_id)
        root = ElementTree.fromstring(xml)
        value = root.findtext("c6")
        print(int(value) if value is not None else -1)
        return 0
    except (OSError, ValueError, ElementTree.ParseError, URLError):
        print(-1)
        return 0


if __name__ == "__main__":
    sys.exit(main())
