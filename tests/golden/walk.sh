#!/bin/sh
# Sprint-01 discovery check: aspen's traversal must discover exactly the entries
# tree does (set equality, order-independent), with directories typed correctly.
# Oracle: `tree -fiN --noreport -n` (full paths, no indent, no report, names
# as-is); first line is the root, dropped; symlink " -> target" suffixes
# stripped. -N is essential: without it tree escapes spaces/UTF-8 bytes in a
# locale-dependent way (octal in the C locale on macOS), which spuriously differs
# from aspen's raw --asp-debug-walk output even when the path set matches.
set -u

work=tests/.work
ref="$work/ref/tree-2.3.2"
corpus="$work/corpus"
ASP=./aspen

[ -x "$ASP" ] || { echo "WALK: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null || { echo "WALK: no ref tree"; exit 1; }
[ -d "$corpus" ] || sh tests/golden/mkcorpus.sh "$corpus" >/dev/null

fail=0

check() { # <label> <aspen-cmd-output-file> <tree-output-file>
	if ! diff -u "$2" "$3" >/dev/null 2>&1; then
		echo "WALK: $1 differs"
		diff -u "$3" "$2" | sed -n '1,12p'
		fail=1
	fi
}

# All discovered paths (default: no dotfiles)
"$ASP" --asp-debug-walk "$corpus" | cut -f2- | sort >"$work/wk.asp"
"$ref" -fiN --noreport -n "$corpus" | sed '1d; s/ -> .*//' | sort >"$work/wk.ref"
check "path set (default)" "$work/wk.asp" "$work/wk.ref"

# Directory paths only — validates DIR typing / descent
"$ASP" --asp-debug-walk "$corpus" | awk -F'\t' '$1=="d"{print $2}' | sort >"$work/wk.dasp"
"$ref" -dfiN --noreport -n "$corpus" | sed '1d' | sort >"$work/wk.dref"
check "dir set (default)" "$work/wk.dasp" "$work/wk.dref"

# With dotfiles (-a / --all)
"$ASP" --asp-debug-walk -a "$corpus" | cut -f2- | sort >"$work/wk.aasp"
"$ref" -afiN --noreport -n "$corpus" | sed '1d; s/ -> .*//' | sort >"$work/wk.aref"
check "path set (-a)" "$work/wk.aasp" "$work/wk.aref"

if [ "$fail" -eq 0 ]; then
	echo "WALK: discovery matches tree (paths + dirs, default and -a)"
else
	exit 1
fi
