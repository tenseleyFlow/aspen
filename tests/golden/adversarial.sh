#!/bin/sh
# Adversarial regression fixtures (SR02-3.6). Hostile inputs the security lens fed
# aspen during the re-audit and which it survived — locked in here so a future
# refactor can't silently reintroduce a crash, an over-read, or a divergence from
# tree. Each case asserts aspen matches tree (program name aside) and exits sanely
# (no signal). Run the release binary; the ASan/UBSan CI job runs the same inputs
# for memory-safety. POSIX sh: scratch goes through temp files, not <(...).
set -u
refroot=tests/.work
ref="$refroot/ref/tree-2.3.2"
ASP=$(cd "$(dirname "$0")/../.." && pwd)/aspen
adv=$(mktemp -d "${TMPDIR:-/tmp}/aspadv.XXXXXX") || { echo "ADVERSARIAL: mktemp failed"; exit 1; }
trap 'chmod -R u+rwx "$adv" 2>/dev/null; rm -rf "$adv"' EXIT INT TERM
fail=0

[ -x "$ASP" ] || { echo "ADVERSARIAL: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null 2>&1 || { echo "ADVERSARIAL: no ref"; exit 1; }

norm() { sed 's/^tree: /PROG: /; s/^aspen: /PROG: /'; }

# cmp_parity <label> ... runs $ref then $ASP with the SAME argv. The PRIMARY
# assertion is that aspen survives (no signal). Parity-with-tree is checked only
# when tree ALSO survives — on some hostile inputs tree itself crashes (e.g. macOS
# SIGTRAPs on a 64KB --fromfile line); aspen surviving where tree dies is a win,
# not a divergence, so we note it and pass.
cmp_parity() {
	_lbl=$1; shift
	"$ref" "$@" >"$adv/r.o" 2>"$adv/r.e"; _rrc=$?
	"$ASP" "$@" >"$adv/a.o" 2>"$adv/a.e"; _arc=$?
	if [ "$_arc" -ge 128 ]; then
		echo "ADVERSARIAL[$_lbl]: aspen killed by signal (rc=$_arc)"; fail=1; return
	fi
	if [ "$_rrc" -ge 128 ]; then
		echo "ADVERSARIAL[$_lbl]: tree crashed (rc=$_rrc); aspen survived (rc=$_arc) — robustness win, parity skipped"
		return
	fi
	norm <"$adv/r.e" >"$adv/r.en"; norm <"$adv/a.e" >"$adv/a.en"
	if ! cmp -s "$adv/r.o" "$adv/a.o" || ! cmp -s "$adv/r.en" "$adv/a.en" || [ "$_rrc" != "$_arc" ]; then
		echo "ADVERSARIAL[$_lbl]: diverges from tree (rc $_rrc/$_arc)"
		diff "$adv/r.o" "$adv/a.o" 2>/dev/null | sed -n '1,4p'
		diff "$adv/r.en" "$adv/a.en" 2>/dev/null | sed -n '1,4p'
		fail=1
	fi
}

# 1) .gitignore whose last line is a lone backslash, after a longer line — the
#    asp_gittrim trailing-backslash over-read (SR02-0.8). Both tools share the bug
#    in-bounds and must emit identical output.
g="$adv/g"; mkdir -p "$g"; : > "$g/abcdefghij"; : > "$g/keep"; : > "$g/zzz"
printf 'abcdefghij\n\\' > "$g/.gitignore"
cmp_parity "gitignore-trailing-backslash" --gitignore "$g"

# 2) A single ~64KB --fromfile line (a path far longer than PATH_MAX). Must not
#    crash; tree truncates/handles it deterministically and aspen must match.
big=$(awk 'BEGIN{s="";for(i=0;i<65536;i++)s=s "a";print s}')
printf '%s\n' "$big" > "$adv/ff.txt"
cmp_parity "fromfile-64k-line" --fromfile "$adv/ff.txt"

# 3) A ~600KB TREE_COLORS value under -C. The color parser must not overrun; output
#    matches tree. (Forcecolor so the map is actually parsed.)
huge=$(awk 'BEGIN{s="";for(i=0;i<60000;i++)s=s "*.x=01;32:";print s}')
c="$adv/c"; mkdir -p "$c"; : > "$c/f.x"; : > "$c/g.txt"
TREE_COLORS="$huge" "$ASP" -C -n "$c" >"$adv/a.o" 2>"$adv/a.e"; arc=$?
TREE_COLORS="$huge" "$ref" -C -n "$c" >"$adv/r.o" 2>"$adv/r.e"; rrc=$?
if [ "$arc" -ge 128 ]; then
	echo "ADVERSARIAL[tree_colors-600k]: aspen killed by signal (rc=$arc)"; fail=1
elif [ "$rrc" -ge 128 ]; then
	echo "ADVERSARIAL[tree_colors-600k]: tree crashed (rc=$rrc); aspen survived — robustness win, parity skipped"
else
	cmp -s "$adv/r.o" "$adv/a.o" || { echo "ADVERSARIAL[tree_colors-600k]: -C output diverges"; fail=1; }
fi

if [ "$fail" -eq 0 ]; then
	echo "ADVERSARIAL: hostile inputs survive + match tree (gitignore-backslash, 64k fromfile, 600k TREE_COLORS)"
else
	exit 1
fi
