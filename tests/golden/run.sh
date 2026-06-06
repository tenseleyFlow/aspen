#!/bin/sh
# Golden parity harness.
#   Phase 1 (ref-vs-ref): validates the differ + corpus determinism. Always GATES.
#   Phase 2 (aspen-vs-ref): the real parity check. Runs+GATES only once
#                           tests/golden/PARITY_ACTIVE exists (created in Sprint 02,
#                           when aspen first produces real output).
#
# The ONLY sanctioned difference is the leading program-name token on stderr
# diagnostic lines, which is normalized before comparison. stdout is never
# normalized. See .docs/audits/01 + .docs/sprints/sprint-02.
set -u

here=tests/golden
work=tests/.work
ref="$work/ref/tree-2.3.2"
corpus="$work/corpus"
ASP=./aspen

mkdir -p "$work"
sh "$here/build-ref.sh" 2.3.2 || { echo "GOLDEN: cannot build reference tree"; exit 1; }
sh "$here/mkcorpus.sh" "$corpus" >/dev/null

# Flag-sets compared on the corpus; %C expands to the corpus path. Grows per sprint.
CASES='%C
-a %C
-d %C
-F %C
-aF %C
--noreport %C
-L 2 %C
--version
--help
/no/such/path-xyz'

normprog() { sed 's/^tree: /PROG: /; s/^aspen: /PROG: /'; }

run_case() { # <bin> <case-string>
	_bin=$1
	_expanded=$(printf '%s' "$2" | sed "s#%C#$corpus#g")
	_oi=$IFS
	IFS=' 	'
	# shellcheck disable=SC2086
	set -- $_expanded
	IFS=$_oi
	"$_bin" "$@" >"$work/o.out" 2>"$work/o.err"
	echo $? >"$work/o.rc"
}

phase() { # <bin_a> <bin_b> <label>  -> echoes diff count, returns it (capped 125)
	_a=$1; _b=$2; _label=$3; _fails=0; _n=0
	_oifs=$IFS
	IFS='
'
	for _c in $CASES; do
		[ -n "$_c" ] || continue
		_n=$((_n + 1))
		run_case "$_a" "$_c"
		cp "$work/o.out" "$work/a.out"; normprog <"$work/o.err" >"$work/a.err"; cp "$work/o.rc" "$work/a.rc"
		run_case "$_b" "$_c"
		cp "$work/o.out" "$work/b.out"; normprog <"$work/o.err" >"$work/b.err"; cp "$work/o.rc" "$work/b.rc"
		if ! diff -q "$work/a.out" "$work/b.out" >/dev/null 2>&1 ||
		   ! diff -q "$work/a.err" "$work/b.err" >/dev/null 2>&1 ||
		   [ "$(cat "$work/a.rc")" != "$(cat "$work/b.rc")" ]; then
			_fails=$((_fails + 1))
			echo "  DIFF [$_label]: $_c"
		fi
	done
	IFS=$_oifs
	echo "$_label: $_n cases, $_fails diffs" >&2
	return $_fails
}

# Phase 1 — harness self-test
phase "$ref" "$ref" "self-test(ref-vs-ref)" >/dev/null
selffail=$?

# Phase 2 — parity (gated)
parityfail=0
if [ -f "$here/PARITY_ACTIVE" ] && [ -x "$ASP" ]; then
	phase "$ASP" "$ref" "parity(aspen-vs-ref)" >/dev/null
	parityfail=$?
fi

rc=0
if [ "$selffail" -ne 0 ]; then
	echo "GOLDEN: self-test FAILED — harness or corpus is non-deterministic"
	rc=1
fi
if [ -f "$here/PARITY_ACTIVE" ] && [ "$parityfail" -ne 0 ]; then
	echo "GOLDEN: parity FAILED ($parityfail diffs vs tree 2.3.2)"
	rc=1
fi
if [ "$rc" -eq 0 ]; then
	if [ -f "$here/PARITY_ACTIVE" ]; then
		echo "GOLDEN: ok (self-test clean, parity enforced)"
	else
		echo "GOLDEN: ok (self-test clean; parity gate inactive until Sprint 02)"
	fi
fi
exit $rc
