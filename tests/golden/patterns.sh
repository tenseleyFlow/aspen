#!/bin/sh
# Slashed -P/-I pattern parity (audit A4). tree 2.3.2 matches a -P/-I pattern
# against an entry's BASENAME, its full path from the walk root, AND every
# '/'-suffix of that path (file.c:132, patinclude/patignore with checkpaths).
# aspen was first ported from tree 2.2.1, which matched the basename only, so any
# pattern containing '/' silently matched nothing. This locks in the fix across
# the streaming, -J/-X, -L and --matchdirs paths.
#
# Known residual (NOT covered here): tree's getfulltree consumers (--du/--prune/-d)
# additionally apply a "directory whose name/path matches -P shows its whole
# subtree" (matched/pattern=0) that tree's own streaming and -J/-X paths do NOT —
# a tree self-inconsistency. aspen is consistent (matchdirs-gated everywhere), so
# `--du -P 'a/*'` differs by tree showing a sub-subtree file. Documented in
# deviations.md; excluded here.
set -u
refroot=tests/.work
ref="$refroot/ref/tree-2.3.2"
ASP=$(cd "$(dirname "$0")/../.." && pwd)/aspen
pp=$(mktemp -d "${TMPDIR:-/tmp}/asppat.XXXXXX") || { echo "PATTERNS: mktemp failed"; exit 1; }
trap 'rm -rf "$pp"' EXIT INT TERM
fail=0

[ -x "$ASP" ] || { echo "PATTERNS: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null 2>&1 || { echo "PATTERNS: no ref"; exit 1; }

mkdir -p "$pp/t/a/aa" "$pp/t/b"
: > "$pp/t/a/file1.txt"; : > "$pp/t/a/aa/deep.h"; : > "$pp/t/a/drop.log"
: > "$pp/t/top.txt"; : > "$pp/t/b/b1.txt"; : > "$pp/t/b/b2.h"

set -f  # patterns must reach the binary verbatim — no shell globbing
chk() { # flags... (pattern args quoted by the caller via "$@")
	"$ref" "$@" "$pp/t" >"$pp/r" 2>&1
	"$ASP" "$@" "$pp/t" >"$pp/a" 2>&1
	if ! diff "$pp/r" "$pp/a" >/dev/null; then
		echo "PATTERNS[$*]: differs from tree"; diff "$pp/r" "$pp/a" | sed -n '1,8p'; fail=1
	fi
}

# streaming + -J/-X/-L/--matchdirs across basename / full-path / suffix / ** patterns
chk -P 'a/*'
chk -P 'a/**'
chk -P '*/*.txt'
chk -P 'aa/*.h'
chk -P 'a/file1.txt'
chk -P '*.txt'
chk -P 'a/aa/*'
chk -P 'b/*'
chk -I 'a/*.log'
chk -I '*/*.h'
chk -I 'b/*.h'
chk -P 'a/*' -L 2
chk -J -P 'a/*'
chk -X -P 'a/*'
chk --matchdirs -P 'a/*'
chk --matchdirs -P 'aa/*'
chk -a -P 'a/*'

if [ "$fail" -eq 0 ]; then
	echo "PATTERNS: slashed -P/-I match basename+path+suffix == tree 2.3.2 (streaming/-J/-X/-L/--matchdirs)"
else
	exit 1
fi
