#!/bin/sh
# Fixture for --condense (singleton-dir-chain collapse). Covers: a chain ending
# in a file (collapses, the file hangs under the joined line), a chain ending in
# an empty dir (collapses, nothing under), a dir with two children (not a
# singleton), a dir whose lone child is a file (not collapsed), and a symlink
# absorbed into a chain (with -l it is followed; the real target is still
# descended even though reached via the symlink — tree only marks symlinks
# "recursive, not followed").
set -eu

root=${1:?usage: mkcondense.sh DIR}
rm -rf "$root"
mkdir -p "$root/a/b/c/leaf" "$root/single/only/here" \
	 "$root/multi/d1" "$root/multi/d2" "$root/x" \
	 "$root/realdir/sub" "$root/p"
: > "$root/a/b/c/leaf/deep.txt"
: > "$root/x/f1"
: > "$root/realdir/sub/f"
ln -s ../realdir "$root/p/link"

echo "condense fixture at $root"
