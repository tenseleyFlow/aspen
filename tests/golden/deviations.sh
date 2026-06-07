#!/bin/sh
# Intentional-deviation assertions (see .docs/deviations.md). For each documented
# place where aspen deliberately does the CORRECT thing instead of copying a tree
# bug, assert aspen's correct behaviour AND (for context) show tree's buggy one.
# This catches a regression that would "re-bug" aspen back toward tree.
set -u
work=tests/.work
ref="$work/ref/tree-2.3.2"
ASP=./aspen
dv="$work/dev"
fail=0

[ -x "$ASP" ] || { echo "DEVIATIONS: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null || { echo "DEVIATIONS: no ref"; exit 1; }

# Fresh fixtures: an unreadable child dir, and a dir over a small --filelimit.
rm -rf "$dv"; mkdir -p "$dv/perm/sub/inner" "$dv/fl/big"
chmod 000 "$dv/perm/sub/inner"
i=0; while [ "$i" -lt 5 ]; do : > "$dv/fl/big/f$i"; i=$((i + 1)); done

# D1: aspen exits 2 on any open/filelimit error, in every mode; tree wrongly
# exits 0 in the full-tree modes. Output identical, only rc deviates.
d1() { # <desc> <target> <flags...>
	_d=$1; _t=$2; shift 2
	"$ASP" "$@" "$_t" >"$dv/a.o" 2>"$dv/a.e"; arc=$?
	"$ref" "$@" "$_t" >"$dv/b.o" 2>"$dv/b.e"; brc=$?
	if ! diff -q "$dv/a.o" "$dv/b.o" >/dev/null 2>&1; then
		echo "DEVIATIONS D1 [$_d]: stdout should still match tree"; diff "$dv/b.o" "$dv/a.o" | sed -n '1,6p'; fail=1; return
	fi
	if [ "$arc" != 2 ]; then
		echo "DEVIATIONS D1 [$_d]: aspen rc=$arc, expected 2 (regressed toward tree's rc=$brc)"; fail=1
	fi
}

d1 "--du perm"         "$dv/perm" --du
d1 "--prune perm"      "$dv/perm" --prune
d1 "--matchdirs perm"  "$dv/perm" --matchdirs
d1 "--du -J perm"      "$dv/perm" --du -J
d1 "--filelimit+du"    "$dv/fl"   --filelimit 3 --du
d1 "--filelimit+prune" "$dv/fl"   --filelimit 3 --prune

# D2: -J stays valid JSON after an error — a later plain file must NOT get a
# spurious "contents" key (tree's flag.J && errors quirk). fl/big trips the
# filelimit (an error); fl/small/a is a later plain file.
"$ASP" -J --filelimit 3 "$dv/fl" >"$dv/j.o" 2>/dev/null
if grep -q '"name":"a","contents"' "$dv/j.o" || grep -q '"type":"file"[^}]*"contents"' "$dv/j.o"; then
	echo "DEVIATIONS D2: -J emitted a spurious contents on a file after an error (regressed to tree's quirk)"
	grep -n '"type":"file"' "$dv/j.o" | sed -n '1,4p'; fail=1
else
	# sanity: the error itself must still be present (so we tested the right path)
	grep -q 'exceeds filelimit' "$dv/j.o" || { echo "DEVIATIONS D2: setup wrong, no filelimit error in -J output"; fail=1; }
fi

# D3: percent-encoding uses the real byte. A UTF-8 name under -H must encode to
# %C3%A9, not tree's sign-extended %FFFFFFC3%FFFFFFA9.
mkdir -p "$dv/ue"; : > "$dv/ue/café"
"$ASP" -H . "$dv/ue" >"$dv/h.o" 2>/dev/null
if grep -q '%FFFFFF' "$dv/h.o"; then
	echo "DEVIATIONS D3: -H href sign-extended a high byte (regressed to tree's bug)"
	grep -oE 'href="[^"]*caf[^"]*"' "$dv/h.o" | head -1; fail=1
elif ! grep -q 'caf%C3%A9' "$dv/h.o"; then
	echo "DEVIATIONS D3: expected caf%C3%A9 in href, got:"; grep -oE 'href="[^"]*caf[^"]*"' "$dv/h.o" | head -1; fail=1
fi

chmod -R u+rwx "$dv" 2>/dev/null || :
rm -rf "$dv"

if [ "$fail" -eq 0 ]; then
	echo "DEVIATIONS: all documented deviations hold (aspen correct where tree bugs)"
else
	exit 1
fi
