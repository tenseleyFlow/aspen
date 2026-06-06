#!/bin/sh
# A deep (but under-PATH_MAX) directory chain for deep-traversal parity. The
# golden corpora were all shallow, so deep recursion was never diffed against
# tree. Depth is kept well under PATH_MAX (1024 on *BSD/macOS) so the full path
# never triggers tree's ENAMETOOLONG lstat-drop — aspen and tree stay identical.
# (The >PATH_MAX divergence is the documented lstat-failure-drop stance.)
set -eu
d=${1:?usage: mkdeep.sh DIR}
DEPTH=${2:-250}
rm -rf "$d"
mkdir -p "$d"
p="$d"
i=0
while [ "$i" -lt "$DEPTH" ]; do
	p="$p/d"
	i=$((i + 1))
done
mkdir -p "$p"
# A couple of leaves to exercise report counts and the last-entry connectors.
: > "$p/leaf.txt"
: > "$d/d/top.txt"
