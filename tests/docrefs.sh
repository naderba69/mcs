#!/bin/sh
# Doc-reference sweep.
#
# Every `file.c:NNN` reference in the documentation is a claim that can be
# checked, and writing them from memory has been the single most frequent source
# of error in this project. This checks two things for every reference:
#
#   1. the file exists under mcs/src/
#   2. it has at least NNN lines, so the reference points at real code
#
# Two things it CANNOT detect, both of which have bitten this project:
#
#   - A reference that is merely SHIFTED. The file is still long enough, so the
#     range check passes, but the line now says something else. Editing a .c file
#     invalidates every reference below the edit. This is not hypothetical: the
#     TASK 1.2 hook added ~80 lines to srv-newcamd.c and silently invalidated
#     ~20 references across four documents, all of which still passed this sweep.
#     After changing a .c file, re-grep the docs for that filename and resolve
#     each claim by CONTENT, not by adding the shift -- references written at
#     different times are relative to different file states, so no single offset
#     is correct for all of them.
#   - A bare `:NNN` reference with no filename, e.g. "at `:846`". Those have to
#     be checked by eye against the sentence they sit in.
#
#   make -C tests docrefs

# Resolve everything from the script's own directory, not the caller's. The
# fallback branch below looks for harness files such as ncclient.c and
# cachepeer.c next to this script, and a relative SRC/DOCS made the whole sweep
# report a spurious "no such file" whenever it was run from anywhere but
# mcs/tests/ -- which is exactly the kind of false failure that trains you to
# ignore the real ones.
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SRC=${SRC:-$HERE/../src}
DOCS=${DOCS:-$HERE/../docs}
set -- $DOCS/*.md $HERE/../../CHANGELOG.md

total=0
bad=0

for doc in "$@"; do
    [ -f "$doc" ] || continue
    for ref in $(grep -oE '[A-Za-z0-9_-]+\.(c|h):[0-9]+' "$doc" 2>/dev/null | sort -u); do
        f=${ref%:*}
        n=${ref##*:}
        total=$((total+1))
        if [ -f "$SRC/$f" ]; then
            tgt=$SRC/$f
        elif [ -f "$HERE/$f" ]; then
            tgt=$HERE/$f
        else
            echo "  [FAIL] $doc -> $ref : no such file (looked for $SRC/$f)"
            bad=$((bad+1))
            continue
        fi
        lines=$(wc -l < "$tgt")
        if [ "$n" -gt "$lines" ]; then
            echo "  [FAIL] $doc -> $ref : file has only $lines lines"
            bad=$((bad+1))
        fi
    done
done


# ---------------------------------------------------------------------------
# Second pass, ADVISORY ONLY: is the cited line plausibly the line the sentence
# is talking about?
#
# The first pass cannot see a SHIFTED reference: the file is still long enough,
# so the range check passes while the line now says something else. That is not
# hypothetical -- the TASK 1.2 hook added ~80 lines to srv-newcamd.c and
# silently invalidated ~20 references, all of which passed; the TASK 2.1 work
# shifted 41 more across five documents.
#
# This pass reads the sentence the reference sits in, takes the identifiers it
# names (`foo()`, `bar[16]`), and reports when none of them appears anywhere
# near the cited line. What it CANNOT do: prove a reference right. A sentence
# may name nothing, a symbol may legitimately live in three places, and the
# nearest match may be a prototype rather than the definition -- so this pass
# NEVER changes the exit status. It is a prompt to look, which is all it needs
# to be. TOL is how far away a named symbol may sit before it is worth a
# second look rather than a re-read.
TOL=15
adv_tmp=$(mktemp) || exit 1
trap 'rm -f "$adv_tmp"' EXIT INT TERM
adv_total=0
adv_seen=0

# CHANGELOG.md is deliberately EXCLUDED here. Its entries are a historical
# record: "this was at :472" was true when it was written, and rewriting those
# numbers to match today's tree would destroy the only evidence of when the code
# moved. The same is true of any document that describes a past state.
for doc in "$@"; do
    [ -f "$doc" ] || continue
    case $(basename "$doc") in CHANGELOG.md) continue;; esac
    grep -nE '[A-Za-z0-9_-]+\.(c|h):[0-9]+' "$doc" 2>/dev/null > "$adv_tmp"
    while IFS= read -r hit; do
        dl=${hit%%:*}
        text=${hit#*:}
        # identifiers named by the sentence: backticked, >=5 chars, longest first
        # (the most specific name is the one a sentence is usually anchored to),
        # and never a file stem -- "`setdcw.c`" would otherwise match the words
        # "setdcw" and every reference to that file would look shifted.
        ids=$(printf '%s\n' "$text" | grep -oE '`[A-Za-z_][A-Za-z0-9_]{4,}' | tr -d '`')
        [ -n "$ids" ] || continue
        for ref in $(printf '%s\n' "$text" | grep -oE '[A-Za-z0-9_-]+\.(c|h):[0-9]+'); do
            af=${ref%:*}
            an=${ref##*:}
            if [ -f "$SRC/$af" ]; then atgt=$SRC/$af
            elif [ -f "$HERE/$af" ]; then atgt=$HERE/$af
            else continue; fi
            adv_total=$((adv_total+1))
            best=-1; bestname=; bestline=
            for id in $(printf '%s\n' "$ids" | awk '{ print length($0), $0 }' | sort -rn | cut -d' ' -f2-); do
                case "$text" in *"$id.c"*|*"$id.h"*) continue;; esac
                # CLOSEST occurrence, not the first: a symbol may be declared in
                # a header-style block at the top of the file and defined far
                # below, and "the first match" then reports a shift that is not
                # there.
                ln=$(grep -nw -- "$id" "$atgt" 2>/dev/null | awk -F: -v t="$an" '
                        BEGIN { m=-1 }
                        { d=$1-t; if (d<0) d=-d; if (m<0 || d<m) { m=d; l=$1 } }
                        END { print l }')
                [ -n "$ln" ] || continue
                d=$((ln-an)); [ "$d" -lt 0 ] && d=$((-d))
                if [ "$best" -lt 0 ] || [ "$d" -lt "$best" ]; then
                    best=$d; bestname=$id; bestline=$ln
                fi
                [ "$best" -le "$TOL" ] && break
            done
            if [ "$best" -ge 0 ] && [ "$best" -gt "$TOL" ]; then
                echo "  [warn] $(basename "$doc"):$dl -> $ref : no identifier from that sentence near it ($bestname is at $bestline)"
                adv_seen=$((adv_seen+1))
            fi
        done
    done < "$adv_tmp"
done

echo "  doc-ref sweep: $total references checked, $bad out of range"
echo "  doc-ref content check (advisory): $adv_total inspected, $adv_seen worth a second look"
[ "$bad" = 0 ]
