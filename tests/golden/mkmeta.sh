#!/bin/sh
# Fixture for metadata columns: known perms and pinned (old) mtimes so -D shows
# the stable "%b %e  %Y" form rather than the clock-relative "%b %e %R". Sizes and
# owners are whatever the runner sees, but tree and aspen observe the same values.
set -eu

root=${1:?usage: mkmeta.sh DIR}
rm -rf "$root"
mkdir -p "$root/sub"
: > "$root/file.txt"; chmod 644 "$root/file.txt"
: > "$root/exec";     chmod 755 "$root/exec"
: > "$root/sub/inner"
# pin mtimes last (creating children bumps the dir mtime)
touch -t 202001020304 "$root/file.txt" "$root/exec" "$root/sub/inner" "$root/sub" "$root"

echo "meta fixture at $root"
