#!/bin/sh
# Symlink fixture for -l / -F: a symlink to a directory, and a symlink that forms
# a cycle when followed (exercises cycle detection / "recursive, not followed").
set -eu

root=${1:?usage: mklnk.sh DIR}
rm -rf "$root"
mkdir -p "$root/real/sub"
: > "$root/real/sub/leaf.txt"
: > "$root/real/top.txt"
ln -s real "$root/tolink"          # symlink -> directory
ln -s ../.. "$root/real/sub/loop"  # cycle when followed

echo "lnk fixture at $root"
