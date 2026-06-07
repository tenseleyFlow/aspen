#!/bin/sh
# -o FILE content parity (SR-0.4). The golden matrix can only see stdout (which
# -o makes empty); this verifies the WRITTEN FILE byte-matches tree's stdout, for
# a few formats, and that stdout really is empty.
set -u
work=tests/.work
ref="$work/ref/tree-2.3.2"
ASP=./aspen
meta="$work/meta"
fail=0

[ -x "$ASP" ] || { echo "OUTFILE: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null || { echo "OUTFILE: no ref"; exit 1; }

chk() { # <desc> <flags...>
	_d=$1; shift
	"$ASP" -o "$work/of.a" "$@" "$meta" >"$work/of.stdout" 2>/dev/null
	if [ -s "$work/of.stdout" ]; then
		echo "OUTFILE: stdout not empty for [$_d] (-o ignored?)"; fail=1; return
	fi
	"$ref" -n "$@" "$meta" >"$work/of.ref" 2>/dev/null
	if ! diff -q "$work/of.ref" "$work/of.a" >/dev/null 2>&1; then
		echo "OUTFILE: written file differs from tree for [$_d]"
		diff "$work/of.ref" "$work/of.a" | sed -n '1,6p' | cat -v
		fail=1
	fi
}

chk "default"
chk "-s -D"   -s -D
chk "-J"      -J
chk "-X"      -X

# open-failure parity: rc 1 + tree's wording (program name aside)
"$ASP" -o /no/such/dir/x "$meta" 2>"$work/of.err"; arc=$?
"$ref" -n -o /no/such/dir/x "$meta" 2>"$work/of.referr";
if [ "$arc" != 1 ]; then echo "OUTFILE: open-failure rc=$arc (expected 1)"; fail=1; fi
if ! diff <(sed 's/^aspen:/PROG:/' "$work/of.err") <(sed 's/^tree:/PROG:/' "$work/of.referr") >/dev/null 2>&1; then
	echo "OUTFILE: open-failure message differs"; fail=1
fi

if [ "$fail" -eq 0 ]; then
	echo "OUTFILE: -o writes the file byte-identically to tree (stdout empty)"
else
	exit 1
fi
