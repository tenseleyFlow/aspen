#!/bin/sh
# .info fixture: a comment file with single- and multi-line annotations and a
# directory annotation.
set -eu

root=${1:?usage: mkinfo.sh DIR}
rm -rf "$root"
mkdir -p "$root/sub"
printf '# a comment\nREADME*\n\tThe readme file.\n*.c\n\tA C source file.\n\tSecond line.\nsub\n\tThe sub directory.\n' > "$root/.info"
: > "$root/README.md"
: > "$root/main.c"
: > "$root/other.txt"
: > "$root/sub/x.c"

echo "info fixture at $root"
