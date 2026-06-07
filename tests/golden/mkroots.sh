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

# Non-directory / special roots — each makes opendir fail, exercising the root
# -F suffix (fifo '|', exec '*', symlink '@') and the HTML root href, both of
# which aspen emitted only on the success path before SR-1.1.
mkfifo "$d/afifo" 2>/dev/null || :               # fifo root           -> '|'
: > "$d/anexec"; chmod +x "$d/anexec"            # executable root     -> '*'
ln -s nonexistent-target "$d/abroken"            # broken symlink root -> '@'
ln -s onlyfiles "$d/asymdir"                     # symlink-to-dir root -> '@'

# Nested error path (SR-1.8): a readable tree with an unreadable dir partway
# down, so "[error opening dir]" appears mid-tree (not at the root) and the walk
# continues past it. Deterministic counterpart to the fuzzer's random errors.
# (Under a root euid — e.g. FreeBSD CI — 0000 is bypassed; aspen still matches
# tree, both just listing it, so the golden case holds either way.)
mkdir -p "$d/derr/keep/leaf" "$d/derr/noread"; : > "$d/derr/keep/leaf/f"
: > "$d/derr/top.txt"; chmod 000 "$d/derr/noread"

# --filelimit: big exceeds, small does not; a file after big exercises tree's
# JSON "flag.J && errors" quirk (later entries get a spurious empty contents).
mkdir -p "$d/fl/big" "$d/fl/small"
i=0; while [ "$i" -lt 5 ]; do : > "$d/fl/big/f$i"; i=$((i + 1)); done
: > "$d/fl/small/a"
