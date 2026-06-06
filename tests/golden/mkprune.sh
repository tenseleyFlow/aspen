#!/bin/sh
# Fixture for --prune / --matchdirs: a dir with content, dirs that become empty,
# and nested-empty dirs.
set -eu

root=${1:?usage: mkprune.sh DIR}
rm -rf "$root"
mkdir -p "$root/a/keep" "$root/b/empty1" "$root/b/empty2" "$root/c" "$root/d/e/f"
: > "$root/a/keep/f.txt"
: > "$root/top.txt"

echo "prune fixture at $root"
