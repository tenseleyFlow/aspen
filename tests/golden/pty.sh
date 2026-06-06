#!/bin/sh
# PTY colorization parity. The golden/color suites redirect stdout to a file, so
# isatty(1) is always false there. This runs aspen and tree 2.3.2 with stdout on
# a real pty slave (via ptyrun) to exercise the auto-color-on-terminal branch
# (color.c: the !isatty path) that file redirection can never reach.
#
# Color output lives entirely in stdout escape codes; the program-name token is
# not involved, so no normalization is needed. Skips cleanly if no pty is free.
set -u

work=tests/.work
ref="$work/ref/tree-2.3.2"
ASP=./aspen
C="$work/corpus"; L="$work/lnk"; M="$work/meta"
pty="$work/ptyrun"
fail=0

[ -x "$ASP" ] || { echo "PTY: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null || { echo "PTY: no ref"; exit 1; }

${CC:-cc} -O2 -o "$pty" tests/golden/ptyrun.c 2>/dev/null || {
	echo "PTY: cannot build ptyrun helper — skipping"; exit 0; }

# Probe: can we actually allocate a pty here (some CI sandboxes can't)?
"$pty" sh -c 'exit 0' 2>/dev/null
if [ $? -eq 2 ]; then
	echo "PTY: no pseudo-terminal available — skipping"; exit 0
fi

LSC="LS_COLORS=di=01;34:ln=01;36:ex=01;32:or=40;31;01:so=01;35:*.txt=00;33:*.log=00;31:"

chk() { # <desc> <env-string> <args...>
	_d=$1; _env=$2; shift 2
	# shellcheck disable=SC2086
	env $_env "$pty" "$ref" "$@" >"$work/pty.t" 2>/dev/null
	# shellcheck disable=SC2086
	env $_env "$pty" "$ASP" "$@" >"$work/pty.a" 2>/dev/null
	if ! diff -q "$work/pty.t" "$work/pty.a" >/dev/null 2>&1; then
		echo "PTY DIFF [$_d]"
		diff "$work/pty.t" "$work/pty.a" | sed -n '1,8p' | cat -v
		fail=1
	fi
}

# Auto-enable paths that REQUIRE a tty (no -C, no CLICOLOR_FORCE):
chk "CLICOLOR+tty default-map" "TERM=xterm CLICOLOR=1"            "$C"
chk "LS_COLORS+tty (implicit)" "TERM=xterm $LSC"                  "$C"
chk "LS_COLORS+tty links"      "TERM=xterm $LSC"                  "$L"
chk "LS_COLORS+tty -F meta"    "TERM=xterm $LSC" -F               "$M"
chk "CLICOLOR+tty -F"          "TERM=xterm CLICOLOR=1" -F         "$C"
# tty but color must stay OFF:
chk "tty, no CLICOLOR -> off"  "TERM=xterm"                       "$C"
chk "NO_COLOR beats CLICOLOR"  "TERM=xterm CLICOLOR=1 NO_COLOR=1" "$C"
chk "-n disables on tty"       "TERM=xterm CLICOLOR=1" -n         "$C"
chk "no TERM -> off on tty"    "CLICOLOR=1"                       "$C"
# tty + forced still matches (force bypasses isatty):
chk "CLICOLOR_FORCE on tty"    "TERM=xterm CLICOLOR_FORCE=1"      "$C"

if [ "$fail" -eq 0 ]; then
	echo "PTY: matches tree (auto-color on a real terminal)"
else
	exit 1
fi
