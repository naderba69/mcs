#!/bin/sh
# Band 121 addendum (121-B), strengthened by 121-C: main must carry the work, and
# say so on demand -- even when the checkout cannot see its own history.
#
# The symptom that started this: fourteen "git thrashes" -- HEAD found reset to the
# base commit with dozens of files modified, and every cycle opening with a rescue
# ritual (fetch the branch, reset --mixed FETCH_HEAD, restore dist/, md5sum).
# The cure was procedural: main contains the session branch's tip, so a reset toward
# main lands ON the work instead of behind it.
#
# What 121-C then found, and why this script changed shape: the workspace was
# re-cloned SHALLOW (`.git/shallow` present, `git rev-list --count HEAD` == 1). In
# that state the old logic is not merely noisy, it is *unprovable*: ancestry needs a
# merge-base, and a grafted clone may not contain one. That is exactly when the
# operator asks "did the files land?" -- so the answer must not depend on history.
#
# Therefore the primary signal is now the TREE, not the commit graph: two refs whose
# tree objects are equal carry identical content, byte for byte, shallow or not.
# Ancestry is still used when it is available, because it proves more (the whole
# line, not just the snapshot).
#
# Needs network (it fetches), so it is NOT in `unit` -- run it via `make -C tests
# merged` after every push.
set -eu
cd "$(dirname "$0")/.."

BR=$(git rev-parse --abbrev-ref HEAD)

# Reachability is asked of the server, never inferred from a fetch that mixes two
# refs: fetching a branch that does not exist on the remote ALSO fails, and treating
# that as "no network" makes an unpushed branch look like a harmless skip -- silence
# read as safety, the exact bug this session keeps hunting.
if ! git ls-remote --heads origin >/dev/null 2>&1; then
    echo "SKIP: cannot reach origin (network needed for this check) -- not a code failure"
    exit 0
fi
git fetch -q origin main 2>/dev/null || true

LOC=$(git rev-parse HEAD)
SHALLOW=$(git rev-parse --is-shallow-repository 2>/dev/null || echo false)

# (0) Ask the server for the branch ref directly, so a never-pushed session branch
# is named as itself. (origin/<branch> tracking refs are not trusted to be current
# in a shallow or restricted-refspec clone.)
RMT=$(git ls-remote origin "refs/heads/$BR" | awk 'NR==1{print $1}')
if [ -z "$RMT" ]; then
    echo "FAIL: origin has no branch '$BR' yet -- the work exists only in this checkout"
    echo "      fix: git push -q origin $BR"
    exit 1
fi
if [ "$LOC" != "$RMT" ]; then
    echo "FAIL: local $LOC != origin/$BR $RMT -- push the branch first"
    exit 1
fi

MAIN=$(git rev-parse --verify -q origin/main 2>/dev/null || true)
if [ -z "$MAIN" ]; then
    MAIN=$(git ls-remote origin refs/heads/main | awk 'NR==1{print $1}')
    if ! git cat-file -e "$MAIN^{commit}" 2>/dev/null; then
        echo "FAIL: cannot read main's commit objects locally (shallow graft?)"
        echo "      fix: git fetch -q origin main && git fetch --unshallow origin"
        exit 1
    fi
fi
LOC_T=$(git rev-parse "HEAD^{tree}")
MAIN_T=$(git rev-parse "$MAIN^{tree}" 2>/dev/null || echo "")
if [ -z "$MAIN_T" ]; then
    echo "FAIL: main's tree object ($MAIN) is not present locally -- cannot compare content"
    echo "      fix: git fetch -q origin main (or --unshallow if this clone is shallow)"
    exit 1
fi

# (1) Content identity: the strongest claim a shallow clone can still make.
if [ "$LOC_T" = "$MAIN_T" ]; then
    echo "merged (content): main's tree == HEAD's tree ($LOC_T)"
    echo "          every file in the deliverable is byte-identical in origin/main."
    [ "$SHALLOW" = "true" ] && \
        echo "          (this checkout is shallow -- history is grafted; the tree hash is"
    [ "$SHALLOW" = "true" ] && \
        echo "           the proof, not the commit count: see SYNC-STATUS-REPORT.md §4د)"
    exit 0
fi

# (2) Trees differ: main does not carry this content. Say how much, and how to prove
# it without guessing. AHEAD is the missing work; EXTRA is main-only plumbing, which
# is EXPECTED after `gh pr merge --merge` and must never be read as a failure.
MISSING="unknown"
EXTRA="?"
if [ "$SHALLOW" != "true" ]; then
    MISSING=$(git rev-list --count "$MAIN".."$LOC")
    EXTRA=$(git rev-list --count "$LOC".."$MAIN")
    if git merge-base --is-ancestor "$LOC" "$MAIN"; then
        # Ancestry holds but trees differ: main has the line plus later commits of
        # its own (another band merged after this branch was cut). Not a failure --
        # rebase/merge main into the branch so the next snapshot is comparable.
        echo "merged+drift: origin/main contains $LOC and advanced by $EXTRA commit(s) since."
        echo "          fix: git merge --no-edit origin/main on $BR, then push."
        exit 1
    fi
    echo "NOT MERGED: origin/main ($MAIN) is missing $MISSING commit(s) of $BR (HEAD=$LOC)."
    if [ "$EXTRA" != "0" ]; then
        echo "          main also carries $EXTRA commit(s) this branch lacks -- merge them into"
        echo "          $BR first (git merge --no-edit origin/main), and never rewrite main to"
        echo "          make this check pass."
    fi
else
    echo "NOT MERGED (content): main's tree ($MAIN_T) != HEAD's tree ($LOC_T)."
    echo "          this checkout is shallow, so commit counts are not trustworthy here;"
    echo "          the tree difference alone is decisive."
    echo "          to see the whole line: git fetch --unshallow origin"
fi
echo "          fix: gh pr create --base main --head $BR ... && gh pr merge <n> --merge"
exit 1
