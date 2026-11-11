#!/usr/bin/env python3
"""Read peer readiness and ECM progress only through the existing web UI.

The live peer-reputation test uses /json for peer state and /cache's existing
XML refresh for the operator-visible Sent REQ counter (c6). It does not infer
readiness or pending work from server, peer, or client logs.
"""

import argparse
import base64
import json
import sys
import time
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
        if tag.lower() != "tr":
            return
        if self.current_id is not None:
            self.current_depth += 1
            return
        row_id = dict(attrs).get("id", "")
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


def fetch(port, path, timeout=3):
    token = base64.b64encode(b"a:a").decode("ascii")
    request = Request("http://127.0.0.1:%d%s" % (port, path),
                      headers={"Authorization": "Basic " + token})
    with urlopen(request, timeout=timeout) as response:
        return response.read()


def read_json(port):
    data = json.loads(fetch(port, "/json"))
    # Save the latest response from the existing web readout for a failed test.
    with open(".pr.json", "w", encoding="utf-8") as snapshot:
        json.dump(data, snapshot, separators=(",", ":"))
    return data


def peer_row(data, peer_port):
    return next((row for row in data.get("cache_peers", [])
                 if int(row.get("port", -1)) == peer_port), None)


def cache_counts(port, peer_port):
    page = fetch(port, "/cache").decode("utf-8", "replace")
    rows = CacheRows()
    rows.feed(page)
    needle = ":%d" % peer_port
    peer_id = next((row_id for row_id, text in rows.rows if needle in text), None)
    if peer_id is None:
        return None
    root = ElementTree.fromstring(fetch(port, "/cache?peerid=%s" % peer_id))
    sent = root.findtext("c6")
    replies = root.findtext("c7")
    if sent is None or replies is None:
        return None
    return int(sent), int(replies)


def get_peer_state(port, peer_port):
    data = read_json(port)
    row = peer_row(data, peer_port)
    if row is None:
        return data, None, None
    return data, row, cache_counts(port, peer_port)


def wait_until(timeout, predicate, description):
    deadline = time.monotonic() + timeout
    latest = None
    while time.monotonic() < deadline:
        try:
            latest = predicate()
            if latest:
                return latest
        except (OSError, ValueError, KeyError, ElementTree.ParseError, URLError,
                json.JSONDecodeError):
            pass
        time.sleep(0.05)
    raise RuntimeError("web UI did not establish %s" % description)


def command_json_ready(args):
    def check():
        data = read_json(args.http_port)
        return data if data.get("version") else None
    data = wait_until(args.timeout, check, "authenticated /json readiness")
    print("WEB /json ready version=%s uptime=%s" %
          (data.get("version", ""), data.get("uptime", "?")))


def command_online(args):
    def check():
        data, row, counts = get_peer_state(args.http_port, args.peer_port)
        if row and counts and int(row.get("ping", -2)) > 0 and int(row.get("cards", 0)) > 0:
            return row, counts
        return None
    row, counts = wait_until(args.timeout, check, "peer online with advertised cards")
    print("WEB peer=%d ONLINE ping=%d cards=%d sent_req=%d received_reply=%d" %
          (args.peer_port, int(row["ping"]), int(row["cards"]), counts[0], counts[1]))


def command_sent(args):
    counts = cache_counts(args.http_port, args.peer_port)
    if counts is None:
        raise RuntimeError("/cache has no row for peer %d" % args.peer_port)
    print(counts[0])


def command_wait_sent(args):
    def check():
        counts = cache_counts(args.http_port, args.peer_port)
        if counts is not None and counts[0] > args.sent_before:
            return counts[0]
        return None
    sent = wait_until(args.timeout, check, "Sent REQ increase from /cache")
    print("WEB Sent REQ peer=%d %d->%d" %
          (args.peer_port, args.sent_before, sent))


def command_replies(args):
    data = read_json(args.http_port)
    row = peer_row(data, args.peer_port)
    if row is None:
        raise RuntimeError("/json has no row for peer %d" % args.peer_port)
    print(int(row.get("ecm_ok", 0)))


def command_hits(args):
    data = read_json(args.http_port)
    row = peer_row(data, args.peer_port)
    if row is None:
        raise RuntimeError("/json has no row for peer %d" % args.peer_port)
    print(int(row.get("hits", 0)))


def command_pending(args):
    def check():
        data, row, counts = get_peer_state(args.http_port, args.peer_port)
        if row is None or counts is None:
            return None
        replies = int(row.get("ecm_ok", 0))
        if replies > args.replies_before:
            raise RuntimeError("reply completed before the web UI observed pending ECM")
        if counts[0] > args.sent_before and replies == args.replies_before:
            return row, counts
        return None
    row, counts = wait_until(args.timeout, check, "outstanding cache ECM request")
    print("WEB ECM pending peer=%d sent_req=%d->%d received_reply=%d hits=%d" %
          (args.peer_port, args.sent_before, counts[0],
           int(row.get("ecm_ok", 0)), int(row.get("hits", 0))))


def command_wait_reply(args):
    def check():
        data = read_json(args.http_port)
        row = peer_row(data, args.peer_port)
        if row is not None and int(row.get("ecm_ok", 0)) > args.replies_before:
            return row
        return None
    row = wait_until(args.timeout, check, "received cache reply in /json")
    print("WEB received cache reply peer=%d ecm_ok=%d" %
          (args.peer_port, int(row["ecm_ok"])))


def command_served(args):
    def check():
        data, row, counts = get_peer_state(args.http_port, args.peer_port)
        if row is None or counts is None:
            return None
        if int(row.get("ecm_ok", 0)) > args.replies_before and \
           int(row.get("hits", 0)) > args.hits_before:
            return row, counts
        return None
    row, counts = wait_until(args.timeout, check, "accepted cache reply and hit")
    print("WEB CW path observed peer=%d received_reply=%d hits=%d" %
          (args.peer_port, int(row["ecm_ok"]), int(row["hits"])))


def command_stage(args):
    expected_last = None if args.last == "-" else args.last
    def check():
        data = read_json(args.http_port)
        row = next((item for item in data.get("peerrep", {}).get("peers", [])
                    if item.get("peer") == "127.0.0.1:%d" % args.peer_port), None)
        if row is None:
            return None
        if int(row.get("stage", -1)) < args.min_stage or \
           int(row.get("events", 0)) < args.min_events:
            return None
        if expected_last is not None and row.get("last") != expected_last:
            return None
        return row
    row = wait_until(args.timeout, check, "peer reputation stage from /json")
    print("WEB peer=%d stage=%d events=%d last=%s" %
          (args.peer_port, int(row["stage"]), int(row["events"]), row.get("last", "")))


def command_history(args):
    data = read_json(args.http_port)
    record = next((item for item in data.get("peerrep", {}).get("peers", [])
                   if item.get("peer") == "127.0.0.1:%d" % args.peer_port), None)
    cache = peer_row(data, args.peer_port)
    if cache is None:
        raise RuntimeError("/json has no cache-peer row for %d" % args.peer_port)
    if args.expected == "present":
        if record is None or int(record.get("stage", -1)) != 0 or \
           int(record.get("events", 0)) < args.min_events or int(cache.get("cards", 0)) <= 0:
            raise RuntimeError("/json does not show history-only stage 0 and online peer")
        print("WEB history-only stage=0 events=%d cards=%d" %
              (int(record["events"]), int(cache["cards"])))
    else:
        if record is not None or int(cache.get("rep_stage", -1)) != -1 or \
           int(cache.get("cards", 0)) <= 0:
            raise RuntimeError("/json does not show cleared history and online peer")
        print("WEB reputation history absent cards=%d" % int(cache["cards"]))


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)

    p = subs.add_parser("json-ready")
    p.add_argument("http_port", type=int)
    p.add_argument("timeout", type=float, nargs="?", default=30.0)
    p.set_defaults(func=command_json_ready)

    p = subs.add_parser("online")
    p.add_argument("http_port", type=int)
    p.add_argument("peer_port", type=int)
    p.add_argument("timeout", type=float, nargs="?", default=35.0)
    p.set_defaults(func=command_online)

    for name, func in (("sent", command_sent), ("replies", command_replies),
                       ("hits", command_hits)):
        p = subs.add_parser(name)
        p.add_argument("http_port", type=int)
        p.add_argument("peer_port", type=int)
        p.set_defaults(func=func)

    p = subs.add_parser("wait-sent")
    p.add_argument("http_port", type=int)
    p.add_argument("peer_port", type=int)
    p.add_argument("sent_before", type=int)
    p.add_argument("timeout", type=float, nargs="?", default=10.0)
    p.set_defaults(func=command_wait_sent)

    p = subs.add_parser("pending")
    p.add_argument("http_port", type=int)
    p.add_argument("peer_port", type=int)
    p.add_argument("sent_before", type=int)
    p.add_argument("replies_before", type=int)
    p.add_argument("timeout", type=float, nargs="?", default=10.0)
    p.set_defaults(func=command_pending)

    p = subs.add_parser("wait-reply")
    p.add_argument("http_port", type=int)
    p.add_argument("peer_port", type=int)
    p.add_argument("replies_before", type=int)
    p.add_argument("timeout", type=float, nargs="?", default=10.0)
    p.set_defaults(func=command_wait_reply)

    p = subs.add_parser("served")
    p.add_argument("http_port", type=int)
    p.add_argument("peer_port", type=int)
    p.add_argument("replies_before", type=int)
    p.add_argument("hits_before", type=int)
    p.add_argument("timeout", type=float, nargs="?", default=10.0)
    p.set_defaults(func=command_served)

    p = subs.add_parser("stage")
    p.add_argument("http_port", type=int)
    p.add_argument("peer_port", type=int)
    p.add_argument("min_stage", type=int)
    p.add_argument("min_events", type=int)
    p.add_argument("last", nargs="?", default="-")
    p.add_argument("timeout", type=float, nargs="?", default=20.0)
    p.set_defaults(func=command_stage)

    p = subs.add_parser("history")
    p.add_argument("http_port", type=int)
    p.add_argument("peer_port", type=int)
    p.add_argument("expected", choices=("present", "absent"))
    p.add_argument("min_events", type=int, nargs="?", default=0)
    p.set_defaults(func=command_history)
    return parser


def main():
    args = build_parser().parse_args()
    try:
        args.func(args)
    except (OSError, RuntimeError, ValueError, ElementTree.ParseError, URLError,
            json.JSONDecodeError) as exc:
        print("WEB CHECK FAILED: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
