#!/bin/sh
# Deterministic fixture corpus for parity tests. Same bytes on every machine:
# fixed names, fixed structure, fixed symlinks. Content is irrelevant to default
# output, so files are 1 byte. mtime-sensitive cases get pinned times later.
set -eu

root=${1:?usage: mkcorpus.sh DIR}
rm -rf "$root"
mkdir -p "$root"

mkdir -p "$root/alpha/sub" "$root/beta" "$root/empty"
printf x > "$root/file.txt"
printf x > "$root/alpha/a1.txt"
printf x > "$root/alpha/a2.log"
printf x > "$root/alpha/sub/deep.txt"
printf x > "$root/beta/b1.dat"
printf x > "$root/.hidden"                       # dotfile (only with -a)

mkdir -p "$root/a dir with spaces"
printf x > "$root/a dir with spaces/inside.txt"  # space in name
printf x > "$root/naïve.txt"                      # multibyte name

ln -s file.txt            "$root/good.link"      # resolvable symlink
ln -s nonexistent-target  "$root/broken.link"    # dangling symlink

# deep chain to exercise indentation bookkeeping
d="$root/deep"
mkdir -p "$d"
i=0
while [ "$i" -lt 8 ]; do
	d="$d/L$i"
	mkdir -p "$d"
	i=$((i + 1))
done
printf x > "$d/bottom"

echo "corpus at $root"
