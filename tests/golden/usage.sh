#!/bin/sh
# Usage / help / version / bad-flag parity (SR-1.6). These outputs differ from
# tree ONLY in the program name (our sanctioned divergence): aspen prints
# "aspen"/"usage: aspen" where tree prints "tree"/"usage: tree". The generic
# golden matrix can't gate them — it normalizes only the leading "PROG:" token
# on stderr, never stdout nor the "usage:" synopsis line — so we do it here,
# normalizing the program name on BOTH streams before diffing.
#
# --version is genuinely aspen's own string (different version + no tree
# copyright), so it is format-asserted rather than diffed against tree.
set -u
work=tests/.work
ref="$work/ref/tree-2.3.2"
ASP=./aspen
fail=0

[ -x "$ASP" ] || { echo "USAGE: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null || { echo "USAGE: no ref"; exit 1; }

# Map either program's name tokens to a neutral PROG, on any line.
norm() { sed 's/^tree: /PROG: /; s/^aspen: /PROG: /; s/^usage: tree /usage: PROG /; s/^usage: aspen /usage: PROG /'; }

# chk <desc> <expected-rc> <args...>: run both tools, compare stdout+stderr+rc
# after program-name normalization.
chk() {
	_d=$1; _erc=$2; shift 2
	"$ASP" "$@" >"$work/u.aout" 2>"$work/u.aerr"; _arc=$?
	"$ref" "$@" >"$work/u.bout" 2>"$work/u.berr"; _brc=$?
	norm <"$work/u.aout" >"$work/u.aoutn"; norm <"$work/u.berr" >"$work/u.berrn"
	norm <"$work/u.bout" >"$work/u.boutn"; norm <"$work/u.aerr" >"$work/u.aerrn"
	if ! diff -q "$work/u.aoutn" "$work/u.boutn" >/dev/null 2>&1; then
		echo "USAGE: stdout differs for [$_d]"; diff "$work/u.boutn" "$work/u.aoutn" | sed -n '1,8p' | cat -v; fail=1
	fi
	if ! diff -q "$work/u.aerrn" "$work/u.berrn" >/dev/null 2>&1; then
		echo "USAGE: stderr differs for [$_d]"; diff "$work/u.berrn" "$work/u.aerrn" | sed -n '1,8p' | cat -v; fail=1
	fi
	if [ "$_arc" != "$_brc" ]; then
		echo "USAGE: rc differs for [$_d]: aspen=$_arc tree=$_brc"; fail=1
	fi
	if [ "$_arc" != "$_erc" ]; then
		echo "USAGE: rc=$_arc for [$_d], expected $_erc"; fail=1
	fi
}

chk "--help"        0 --help
chk "bad-long"      1 --bogus
chk "bad-short"     1 -Z
chk "bad-short-mid" 1 -aZd
chk "missing-arg"   1 -L

# SR02-0.4 (M2): an empty long-option value "--opt=" is a missing argument in tree
# (stderr "Missing argument to --opt=", exit 1, empty stdout) — not a valid empty value.
chk "charset-empty"   1 --charset=
chk "compress-empty"  1 --compress=
chk "filelimit-empty" 1 --filelimit=
chk "timefmt-empty"   1 --timefmt=

# --version: aspen's own line, exit 0. Format-assert (not vs tree).
"$ASP" --version >"$work/u.ver" 2>&1; vrc=$?
if [ "$vrc" != 0 ]; then echo "USAGE: --version rc=$vrc (expected 0)"; fail=1; fi
if ! grep -qE '^aspen v[0-9]+\.[0-9]+\.[0-9]+' "$work/u.ver"; then
	echo "USAGE: --version line malformed:"; cat -v "$work/u.ver"; fail=1
fi

if [ "$fail" -eq 0 ]; then
	echo "USAGE: help/usage/bad-flag byte-identical to tree (name aside); --version ok"
else
	exit 1
fi
