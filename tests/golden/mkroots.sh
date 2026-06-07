#!/bin/sh
# Root-argument edge fixtures (SR-0.6/0.7/0.8). The original golden matrix was
# all-positive and all-directory, so it never tested these — where several real
# parity bugs lived (empty-root over-count, non-dir/unreadable root count+exit,
# --prune -d over-prune, root inode/dev, --noreport in nested formats).
set -eu
d=${1:?usage: mkroots.sh DIR}
rm -rf "$d"
mkdir -p "$d"

mkdir -p "$d/empty"                              # empty directory as root
printf 'hello' > "$d/afile"                      # non-directory (file) as root
mkdir -p "$d/onlyfiles"; : > "$d/onlyfiles/a"; : > "$d/onlyfiles/b"

# --prune -d shape: tree keeps every dir (even truly-empty), prunes nothing.
mkdir -p "$d/pd/hasfile"; : > "$d/pd/hasfile/f"
mkdir -p "$d/pd/hasdir/sub"
mkdir -p "$d/pd/trueempty"
: > "$d/pd/rootfile"

mkdir -p "$d/noread"; chmod 000 "$d/noread"      # unreadable directory as root (rc 0, 1 file)
