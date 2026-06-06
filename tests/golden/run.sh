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
weird="$work/weird"
lnk="$work/lnk"
meta="$work/meta"
vert="$work/vert"
prn="$work/prn"
ASP=./aspen

mkdir -p "$work"
sh "$here/build-ref.sh" 2.3.2 || { echo "GOLDEN: cannot build reference tree"; exit 1; }
sh "$here/mkcorpus.sh" "$corpus" >/dev/null
sh "$here/mkweird.sh" "$weird" >/dev/null
sh "$here/mklnk.sh" "$lnk" >/dev/null
sh "$here/mkmeta.sh" "$meta" >/dev/null
sh "$here/mkvert.sh" "$vert" >/dev/null
sh "$here/mkprune.sh" "$prn" >/dev/null

# Flag-sets compared; %C = corpus, %W = weird names, %L = symlink/cycle fixture.
# Contains ONLY behavior aspen implements so far; grows each sprint (never gate on
# un-implemented flags). Deferred: -s -p -u -g -D (Sprint 04), sort modes (05), ...
CASES='%C
%C %C
--charset=ascii %C
%W
--charset=ascii %W
-a %C
-d %C
-f %C
-i %C
-F %C
-aF %C
-L 2 %C
-L 1 %C
--noreport %C
-x %C
-a %W
-F %W
%L
-d %L
-l %L
-lF %L
-al %L
-F %L
-s %M
-sh %M
--si %M
-p %M
-u %M
-g %M
-pugs %M
--inodes %M
--device %M
-D %M
-cD %M
--timefmt %F %M
--metafirst -ps %M
-F %M
-ps %W
-v %V
--sort=version %V
-rv %V
-U %C
-r %C
--dirsfirst %C
--filesfirst %C
--dirsfirst -r %C
--sort=none %C
--sort=size %M
-t %M
-c %M
-t -r %M
-P *.txt %C
-I *.log %C
-P *.txt -I a* %C
--ignore-case -P *.TXT %C
-aP .h* %C
-P a1*|b1* %C
-P abc| %C
-P zzz* %C
-q %W
-N %W
-Q %W
-qF %W
-NF %W
-Qp %W
--charset=IBM437 %C
--charset=Shift_JIS %C
--charset=EUC-JP %C
--charset=ISO-8859-1 %C
--charset=GB2312 %C
--charset=Big5 %C
--charset=KOI8-R %C
--charset=ISO-2022-JP %C
-A %C
-S %C
-A -F %M
--hyperlink %C
--hyperlink -f %C
--hyperlink --scheme ssh:// %C
--hyperlink --authority example.com %C
--hyperlink --authority . %C
--hyperlink -F %L
--hyperlink %W
--du %C
--du -h %M
--du --si %M
--du -ph %M
--prune %P
--prune -P *.txt %P
--matchdirs -P keep %P
--matchdirs -P b %P
--du --prune %P
--du -L 2 %C
--matchdirs -P alpha %C
-L 0 %C
-L
/no/such/path-xyz'

normprog() { sed 's/^tree: /PROG: /; s/^aspen: /PROG: /'; }

run_case() { # <bin> <case-string>
	_bin=$1
	_expanded=$(printf '%s' "$2" | sed "s#%C#$corpus#g; s#%W#$weird#g; s#%L#$lnk#g; s#%M#$meta#g; s#%V#$vert#g; s#%P#$prn#g")
	_oi=$IFS
	IFS=' 	'
	set -f # no globbing: pattern args like *.txt must reach the binary verbatim
	# shellcheck disable=SC2086
	set -- $_expanded
	set +f
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
