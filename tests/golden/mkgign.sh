#!/bin/sh
# .gitignore fixture: a ROOT .gitignore (so tree doesn't walk parents) exercising
# relative (*.log), directory (build/), root-anchored (/toponly.txt), and
# negation (!important.log) patterns, plus a nested .gitignore.
set -eu

root=${1:?usage: mkgign.sh DIR}
rm -rf "$root"
mkdir -p "$root/build" "$root/sub"
printf '*.log\nbuild/\n!important.log\n/toponly.txt\n' > "$root/.gitignore"
printf 'c.tmp\n' > "$root/sub/.gitignore"
: > "$root/a.txt"
: > "$root/a.log"
: > "$root/important.log"
: > "$root/toponly.txt"
: > "$root/build/x"
: > "$root/sub/toponly.txt"
: > "$root/sub/b.log"
: > "$root/sub/c.tmp"
: > "$root/sub/keep.me"

echo "gitignore fixture at $root"
