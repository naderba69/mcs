#!/bin/sh
# Band 121 addendum (121-B): main must carry the work, and say so on demand.
#
# The symptom was fourteen "git thrashes" in one session: the checkout kept being
# reset to the base commit of main (9b94faa), so every cycle opened with a rescue
# ritual (fetch the branch, reset --mixed to FETCH_HEAD, restore dist/, md5sum the
# binary) and every conclusion risked being read off a half-restored tree. The
# permanent fix is procedural, not clever: main contains the session branch's tip,
# so a reset toward main lands ON the work instead of behind it.
#
# This script is the instrument that keeps it true. It needs network (it fetches),
# so it is NOT part of `unit` -- it is `make -C tests merged`, run after every push.
#
# Order matters: containment is the question, and a PR merge commit on main is
# EXPECTED (gh pr merge --merge adds one), so main being "ahead" by merge
# plumbing must not read as a failure.
#   1. local HEAD == origin/<this branch>   -- nothing unpushed
#   2. HEAD is an ancestor of origin/main   -- the work is in main
#   3. if it is not: report divergence both ways, because main-only commits have to
#      be reconciled into the branch BEFORE opening the PR (never force main)
set -eu
cd "$(dirname "$0")/.."

BR=$(git rev-parse --abbrev-ref HEAD)
if ! git fetch -q origin "$BR" main 2>/dev/null; then
    echo "SKIP: could not fetch origin (network needed for this check) -- not a code failure"
    exit 0
fi

LOC=$(git rev-parse HEAD)
RMT=$(git rev-parse "origin/$BR")
MAIN=$(git rev-parse origin/main)

if [ "$LOC" != "$RMT" ]; then
    echo "FAIL: local $LOC != origin/$BR $RMT -- push the branch first"
    exit 1
fi

AHEAD=$(git rev-list --count "$MAIN".."$LOC")      # our commits, main still without them
EXTRA=$(git rev-list --count "$LOC".."$MAIN")      # main-only commits (merge plumbing, usually)

if git merge-base --is-ancestor "$LOC" "$MAIN"; then
    if [ "$EXTRA" = "0" ]; then
        echo "merged: origin/main == $LOC -- main carries the work, a reset toward main lands on it"
    else
        echo "merged: origin/main contains $LOC plus $EXTRA merge-plumbing commit(s)"
    fi
    exit 0
fi

echo "NOT MERGED: origin/main ($MAIN) is missing $AHEAD commit(s) of $BR (HEAD=$LOC)."
if [ "$EXTRA" != "0" ]; then
    echo "          main also carries $EXTRA commit(s) this branch lacks -- merge them into"
    echo "          $BR first (git merge --no-edit origin/main), and never rewrite main to"
    echo "          make this check pass."
fi
echo "          fix: gh pr create --base main --head $BR ... && gh pr merge <n> --merge"
exit 1
