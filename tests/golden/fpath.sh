#!/bin/sh
# -f (fullpath) root canonicalization parity (reaudit3 f-root-trailing-slash).
# tree strips ALL trailing '/' from a -f root before using it for BOTH the printed
# root line and the path it prefixes onto every child — `tree -f q/` shows root `q`
# and child `q/a`, NOT `q/` and `q//a`. aspen previously stripped only its internal
# c.path (so streaming children were right) but passed the raw root to the renderers,
# so the root line kept the slash and the -J/-X path stack (seeded from that root)
# emitted doubled-slash `q//child` on every entry. This locks the fix across every
# renderer + the no-slash regression. A root that is all slashes (`/`) keeps one;
# without -f the root prints verbatim (tree does NOT strip there).
set -u
ref="tests/.work/ref/tree-2.3.2"
ASP=$(cd "$(dirname "$0")/../.." && pwd)/aspen
fp=$(mktemp -d "${TMPDIR:-/tmp}/aspfp.XXXXXX") || { echo "FPATH: mktemp failed"; exit 1; }
trap 'rm -rf "$fp" "$fp.t" "$fp.s"' EXIT INT TERM
fail=0

[ -x "$ASP" ] || { echo "FPATH: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null 2>&1 || { echo "FPATH: no ref"; exit 1; }

mkdir -p "$fp/q/sub"
: > "$fp/q/a"; : > "$fp/q/sub/b"

# Run every case from inside $fp so the root arg is relative (the common shell/tab-
# completion form that appends the trailing slash).
REF=$(cd "$(dirname "$ref")" && pwd)/$(basename "$ref")
chk() { # <args...> — last arg is the root; run from inside $fp
	( cd "$fp" && LC_ALL=C "$REF" "$@" ) >"$fp.t" 2>&1
	( cd "$fp" && LC_ALL=C "$ASP" "$@" ) >"$fp.s" 2>&1
	if ! diff "$fp.t" "$fp.s" >/dev/null 2>&1; then
		echo "FPATH DIFF: $*"; diff "$fp.t" "$fp.s" | sed -n '1,6p'; fail=1
	fi
}

# Trailing slash: streaming, -J, -X, -H, --du, -d, --prune, -P filter, -L, single + multiple.
for root in "q/" "q///"; do
	chk -f "$root"
	chk -f -J "$root"
	chk -f -X "$root"
	chk -f --du "$root"
	chk -f -d "$root"
	chk -f --prune -P 'a' "$root"
	chk -f -L 1 "$root"
	chk -f -s -p "$root"
done

# Regressions: WITHOUT a trailing slash must be unchanged; a relative '.' root; an
# absolute root with a trailing slash; and NON-f (root prints verbatim, slash kept).
chk -f q
chk -f -J q
chk -f .
chk -f -J .
chk q/          # non-f: tree keeps the trailing slash on the root line
chk -J q/
chk "-f" "$fp/"  # absolute trailing-slash root

if [ "$fail" -eq 0 ]; then
	echo "FPATH: -f root trailing-slash canonical == tree (root line + child paths, all renderers)"
else
	exit 1
fi
