#!/usr/bin/env python3
"""Reject new unbounded strcpy/strcat/sprintf call sites in src/.

The checked-in fingerprint inventory is a migration fence, not a claim that
legacy sites are safe. It records the existing call expressions. Normal test
runs fail if a new expression appears or an existing expression changes;
removals are always allowed. Refresh the inventory only after a deliberate
review of every changed call site.
"""

from __future__ import print_function

import argparse
import collections
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src"
BASELINE = ROOT / "tests" / "unsafe-api-baseline.tsv"
APIS = ("strcpy", "strcat", "sprintf")
CALL_RE = re.compile(r"\b(strcpy|strcat|sprintf)\s*\(")


def _blank_non_newline(chars, start, end):
    for pos in range(start, end):
        if chars[pos] != "\n":
            chars[pos] = " "


def mask_comments_and_literals(text):
    """Blank comments and quoted literals, preserving offsets and newlines."""
    chars = list(text)
    i = 0
    state = "code"
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if c == "/" and n == "/":
                _blank_non_newline(chars, i, i + 2)
                i += 2
                state = "line-comment"
                continue
            if c == "/" and n == "*":
                _blank_non_newline(chars, i, i + 2)
                i += 2
                state = "block-comment"
                continue
            if c == '"':
                chars[i] = " "
                i += 1
                state = "string"
                continue
            if c == "'":
                chars[i] = " "
                i += 1
                state = "char"
                continue
            i += 1
            continue

        if state == "line-comment":
            if c == "\n":
                state = "code"
            else:
                chars[i] = " "
            i += 1
            continue

        if state == "block-comment":
            if c == "*" and n == "/":
                chars[i] = chars[i + 1] = " "
                i += 2
                state = "code"
            else:
                if c != "\n":
                    chars[i] = " "
                i += 1
            continue

        # String and character literals: honor escapes so an escaped quote
        # cannot expose text inside a literal as if it were C code.
        end_quote = '"' if state == "string" else "'"
        if c == "\\":
            chars[i] = " "
            i += 1
            if i < len(text):
                if text[i] != "\n":
                    chars[i] = " "
                i += 1
        elif c == end_quote:
            chars[i] = " "
            i += 1
            state = "code"
        else:
            if c != "\n":
                chars[i] = " "
            i += 1
    return "".join(chars)


def scan_text(path, text):
    """Return (fingerprint Counter, fingerprint -> source line numbers)."""
    masked = mask_comments_and_literals(text)
    counts = collections.Counter()
    locations = collections.defaultdict(list)
    for match in CALL_RE.finditer(masked):
        api = match.group(1)
        open_paren = masked.find("(", match.start(), match.end())
        depth = 0
        end = None
        for pos in range(open_paren, len(masked)):
            if masked[pos] == "(":
                depth += 1
            elif masked[pos] == ")":
                depth -= 1
                if depth == 0:
                    end = pos + 1
                    break
        if end is None:
            line = masked.count("\n", 0, match.start()) + 1
            raise ValueError("{}:{}: unmatched call parentheses for {}".format(path, line, api))
        expression = re.sub(r"\s+", " ", text[match.start():end]).strip()
        fingerprint = hashlib.sha256(expression.encode("utf-8")).hexdigest()
        key = (path, api, fingerprint)
        counts[key] += 1
        locations[key].append(masked.count("\n", 0, match.start()) + 1)
    return counts, locations


def collect_source_calls():
    counts = collections.Counter()
    locations = collections.defaultdict(list)
    files = sorted(p for p in SOURCE.rglob("*") if p.suffix in (".c", ".h"))
    for source in files:
        rel = source.relative_to(ROOT).as_posix()
        file_counts, file_locations = scan_text(rel, source.read_text(encoding="utf-8", errors="replace"))
        counts.update(file_counts)
        for key, lines in file_locations.items():
            locations[key].extend(lines)
    return counts, locations


def read_baseline(path=BASELINE):
    counts = collections.Counter()
    with path.open("r", encoding="utf-8") as handle:
        for line_no, raw in enumerate(handle, 1):
            line = raw.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 4:
                raise ValueError("{}:{}: expected four tab-separated fields".format(path, line_no))
            source, api, fingerprint, number = fields
            if api not in APIS or not re.fullmatch(r"[0-9a-f]{64}", fingerprint):
                raise ValueError("{}:{}: malformed fingerprint row".format(path, line_no))
            count = int(number)
            if count < 1:
                raise ValueError("{}:{}: count must be positive".format(path, line_no))
            counts[(source, api, fingerprint)] += count
    return counts


def write_baseline(counts, path=BASELINE):
    lines = [
        "# Existing unbounded C API call-expression fingerprints; do not refresh without review.",
        "# columns: src-path<TAB>api<TAB>sha256(normalized call expression)<TAB>occurrences",
    ]
    for (source, api, fingerprint), count in sorted(counts.items()):
        lines.append("{}\t{}\t{}\t{}".format(source, api, fingerprint, count))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def totals(counts):
    result = collections.Counter()
    for (_, api, _), count in counts.items():
        result[api] += count
    return result


def self_test():
    sample = '''/* strcpy(fake, fake); */
const char *literal = "sprintf(fake, \\\"%s\\\", fake)";
// strcat(fake, fake);
void f(char *dst, const char *src) {
    strcpy(dst, src);
    strcat(dst, "-tail");
    sprintf(dst,
            "%s:%d", src, 7);
}
'''
    base, _ = scan_text("fixture.c", sample)
    counts = totals(base)
    assert counts == {"strcpy": 1, "strcat": 1, "sprintf": 1}, counts
    doubled = sample.replace("    strcpy(dst, src);", "    strcpy(dst, src);\n    strcpy(dst, src);")
    current, locations = scan_text("fixture.c", doubled)
    additions = current - base
    assert sum(additions.values()) == 1, additions
    key = next(iter(additions))
    assert key[1] == "strcpy" and len(locations[key]) == 2, (key, locations[key])
    print("unsafe-api lint self-test: 3 APIs detected; comments/literals ignored; new call rejected")


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="run parser/regression self-tests")
    parser.add_argument("--refresh-baseline", action="store_true",
                        help="replace the reviewed inventory with current source call sites")
    args = parser.parse_args()

    try:
        if args.self_test:
            self_test()
            return 0
        current, locations = collect_source_calls()
        if args.refresh_baseline:
            write_baseline(current)
            total = totals(current)
            print("baseline refreshed: {} call sites; strcpy={} strcat={} sprintf={}".format(
                sum(current.values()), total["strcpy"], total["strcat"], total["sprintf"]))
            print("review the inventory diff before committing it")
            return 0
        baseline = read_baseline()
    except (OSError, ValueError, AssertionError) as error:
        print("unsafe-api lint: {}".format(error), file=sys.stderr)
        return 2

    additions = current - baseline
    if additions:
        for key, count in sorted(additions.items()):
            source, api, fingerprint = key
            lines = sorted(locations.get(key, []))
            new_lines = lines[-count:]
            for line in new_lines:
                print("[FAIL] new {} call site: {}:{} (fingerprint {})".format(
                    api, source, line, fingerprint[:12]))
        return 1

    removed = sum((baseline - current).values())
    current_totals = totals(current)
    print("unsafe-api lint: {} existing call sites; 0 additions; {} removed".format(
        sum(current.values()), removed))
    print("  strcpy={} strcat={} sprintf={}".format(
        current_totals["strcpy"], current_totals["strcat"], current_totals["sprintf"]))
    return 0


if __name__ == "__main__":
    sys.exit(run())
