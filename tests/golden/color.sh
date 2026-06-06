#!/bin/sh
# Colorization parity. Output is redirected to files (isatty false), so we use
# -C / CLICOLOR_FORCE to exercise the colored path and rely on the piped default
# for the no-color path. The leading program-name token isn't involved (color
# differences are in stdout escape codes), so no normalization is needed.
set -u

work=tests/.work
ref="$work/ref/tree-2.3.2"
ASP=./aspen
C="$work/corpus"; M="$work/meta"; L="$work/lnk"; W="$work/weird"
fail=0

[ -x "$ASP" ] || { echo "COLOR: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null || { echo "COLOR: no ref"; exit 1; }

DEF="TERM=xterm CLICOLOR_FORCE=1"
LSC="TERM=xterm LS_COLORS=di=01;34:ln=01;36:ex=01;32:or=40;31;01:so=01;35:*.txt=00;33:*.log=00;31:"

chk() { # <desc> <env-string> <args...>
	_d=$1; _env=$2; shift 2
	# shellcheck disable=SC2086
	env $_env "$ref" "$@" >"$work/co.t" 2>&1
	# shellcheck disable=SC2086
	env $_env "$ASP" "$@" >"$work/co.a" 2>&1
	if ! diff -q "$work/co.t" "$work/co.a" >/dev/null 2>&1; then
		echo "COLOR DIFF [$_d]"
		diff "$work/co.t" "$work/co.a" | sed -n '1,8p' | cat -v
		fail=1
	fi
}

chk "default-map -C"  "$DEF" -C "$C"
chk "LS_COLORS -C"    "$LSC" -C "$C"
chk "-C -F meta"      "$LSC" -C -F "$M"
chk "-C links"        "$LSC" -C "$L"
chk "ln=target"       "TERM=xterm LS_COLORS=ln=target:di=01;34:or=40;31;01:" -C "$L"
chk "-C -F corpus"    "$LSC" -C -F "$C"
chk "-C -F weird"     "$LSC" -C -F "$W"
chk "piped no-tty"    "TERM=xterm" "$C"
chk "NO_COLOR > CF"   "TERM=xterm CLICOLOR_FORCE=1 NO_COLOR=1" "$C"
chk "-n off"          "$DEF" -n "$C"
chk "CLICOLOR no-tty" "TERM=xterm CLICOLOR=1" "$C"
chk "TERM unset"      "CLICOLOR_FORCE=1" -C "$C"
chk "TREE_CHARSET"    "TREE_CHARSET=IBM437" "$C"
chk "TREE_CHARSET+C"  "TERM=xterm TREE_CHARSET=Shift_JIS CLICOLOR_FORCE=1" -C "$L"

if [ "$fail" -eq 0 ]; then
	echo "COLOR: matches tree (-C / env cases)"
else
	exit 1
fi
