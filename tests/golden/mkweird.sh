#!/bin/sh
# Fixture of awkward filenames to exercise default name escaping (printit):
# spaces, a tab (control char), a backslash, and a multibyte name. Escaping is
# locale-dependent, so the parity suite runs this under both C and UTF-8 locales.
set -eu

root=${1:?usage: mkweird.sh DIR}
rm -rf "$root"
mkdir -p "$root"

: > "$root/normal.txt"
mkdir -p "$root/a dir with spaces"
: > "$root/a dir with spaces/inside.txt"
: > "$root/naïve.txt"
: > "$root/tab$(printf '\t')name"
: > "$root/back\\slash"

echo "weird fixture at $root"
