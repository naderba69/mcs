#!/bin/sh
# Band 119 harness: the header-state audit (tests/staticstate.py).
# Runs on the tree, no build needed -- which is exactly why it belongs in
# `unit`: the bug it guards against compiled and shipped cleanly for 4 bands.
set -e
cd "$(dirname "$0")"
PYTHONDONTWRITEBYTECODE=1 python3 staticstate.py
