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
refroot=tests/.work                 # shared, built once: the reference tree + its source repo
ref="$refroot/ref/tree-2.3.2"
# SR02-3.1/3.2: a RUN-PRIVATE workspace holds the pinned binary, every mutable
# fixture, and the per-case scratch — so a concurrent build/bench that rebuilds
# ./aspen or regenerates a corpus mid-phase can't cause phantom diffs (the audit
# saw ~635), and the trap cleans it (incl. any chmod-000 deviation fixtures) even
# on Ctrl-C. All the $work/* paths below are therefore private to this run.
work=$(mktemp -d "${TMPDIR:-/tmp}/aspgold.XXXXXX") || { echo "GOLDEN: mktemp failed"; exit 1; }
trap 'chmod -R u+rwx "$work" 2>/dev/null; rm -rf "$work"' EXIT INT TERM
corpus="$work/corpus"
weird="$work/weird"
lnk="$work/lnk"
meta="$work/meta"
vert="$work/vert"
prn="$work/prn"
gign="$work/gign"
inf="$work/inf"
ASP="$work/aspen.uut"               # pinned snapshot of ./aspen (set just below)

mkdir -p "$refroot"
sh "$here/build-ref.sh" 2.3.2 || { echo "GOLDEN: cannot build reference tree"; exit 1; }
# Pin the binary under test so a concurrent `gmake release` can't swap it mid-run.
# If ./aspen isn't built, $ASP stays absent and the parity phase is skipped (as before).
[ -x ./aspen ] && cp ./aspen "$ASP"
sh "$here/mkcorpus.sh" "$corpus" >/dev/null
sh "$here/mkweird.sh" "$weird" >/dev/null
sh "$here/mklnk.sh" "$lnk" >/dev/null
sh "$here/mkmeta.sh" "$meta" >/dev/null
sh "$here/mkvert.sh" "$vert" >/dev/null
sh "$here/mkprune.sh" "$prn" >/dev/null
cnd="$work/cnd"
sh "$here/mkcondense.sh" "$cnd" >/dev/null
sh "$here/mkgign.sh" "$gign" >/dev/null
sh "$here/mkinfo.sh" "$inf" >/dev/null
ff="$work/ff"
sh "$here/mkff.sh" "$ff" >/dev/null
dep="$work/deepchain"
sh "$here/mkdeep.sh" "$dep" >/dev/null
rts="$work/roots"
sh "$here/mkroots.sh" "$rts" >/dev/null

# Fixed empty intro/outtro: -H's default document embeds the program name and
# version (legitimately "aspen", not "tree"), so golden-test the HTML *body* with
# --hintro/--houtro replacing that version chrome. %h = intro file, %o = outtro.
: >"$work/hintro"; : >"$work/houtro"

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
%R/empty
-J %R/empty
-X %R/empty
-d %R/empty
--du %R/empty
%R/afile
-J %R/afile
-X %R/afile
-s %R/afile
-J --inodes %R/afile
-d %R/onlyfiles
--prune -d %R/pd
--prune %R/pd
-J --prune -d %R/pd
%R/noread
--noreport -J %R/onlyfiles
--noreport -X %R/onlyfiles
--noreport -d %R/onlyfiles
%R/derr
-J %R/derr
-X %R/derr
-L 2 %R/derr
-s -D %R/derr
%R/afifo
%R/abroken
%R/asymdir
-F %R/afifo
-F %R/anexec
-F %R/abroken
-F %R/asymdir
-dF %R/afifo
-F -s %R/afifo
-l %R/asymdir
-p %R/asymdir
-s %R/asymdir
--inodes %R/asymdir
-pugs %R/asymdir
-J -F %R/afifo
-X -F %R/afifo
-H . --hintro=%h --houtro=%o -F %R/afifo
-J --inodes %M
-X --device %M
-J -f %C
-X -f %C
-J -f %L
-X -f %M
-o %O %M
-o %O -J %M
-o %O -H . --hintro=%h --houtro=%o %M
--filelimit 3 %R/fl
--filelimit 3 -J %R/fl
--filelimit 3 -X %R/fl
--filelimit 3 -H . --hintro=%h --houtro=%o %R/fl
--filelimit 3 -d %R/fl
--filelimit 10 %R/fl
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
-pugsD --timefmt ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ %M
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
--condense %D
--condense -d %D
--condense --du %D
--condense -s %D
--condense -f %D
--condense -F %D
--condense --prune %D
--condense -L 2 %D
--condense -i %D
--condense --noreport %D
--condense -P *.txt %D
-J --condense %D
-X --condense %D
-X --condense --du %D
-J --condense -f %D
-l %D
--compress 2 %V
--compress 3 %V
--compress=1 %V
--compress=-2 %V
--compress 4 %V
--compress 2 -F %C
--charset=ascii --compress 2 %V
--charset=ascii --compress=-3 %V
-J --compress 2 %V
-J --compress 3 %V
-X --compress 2 %V
-X --compress=-1 %V
-H --compress 2 --hintro %h --houtro %o %V
--compress 2 --condense %D
--gitignore %G
--gitignore -a %G
--gitignore -F %G
--gitignore --prune %G
--gitignore -P *.txt %G
--gitignore --ignore-case %G
--gitignore --du %G
--info %I
--info -a %I
--info --charset=ascii %I
--info -F %I
--info --du %I
--infofile %I/.info %I
--gitfile=%G/.gitignore %C
--gitignore --gitfile=%G/.gitignore %G
--opt-toggle %C
-a --opt-toggle -a %C
-n %C
-n -a %C
-R %C
-R -a %C
-H . --hintro=%h --houtro=%o -T MyTitle %C
-J %C
-J -i %C
-J -pugsD %M
-J --du %M
-J -d %C
-J %L
-J --gitignore %G
-J --info %I
-J --prune %P
-J %M %C
-J /no/such/path-xyz
-X %C
-X -i %C
-X -pugsD %M
-X --du %M
-X -d %C
-X %L
-X --info %I
-X --gitignore %G
-X --prune %P
-X --charset=IBM437 %C
-X %M %C
-X /no/such/path-xyz
-H . --hintro=%h --houtro=%o %C
-H . --hintro=%h --houtro=%o -ph %M
-H . --hintro=%h --houtro=%o --nolinks %C
-H . --hintro=%h --houtro=%o %L
-H . --hintro=%h --houtro=%o --info %I
-H . --hintro=%h --houtro=%o -C %C
-H . --hintro=%h --houtro=%o -C %M
-H http://x/y --hintro=%h --houtro=%o %M
-H -base --hintro=%h --houtro=%o %M
-H . --hintro=%h --houtro=%o -d %C
-H . --hintro=%h --houtro=%o -a %C
-H . --hintro=%h --houtro=%o -L 2 %C
-H . --hintro=%h --houtro=%o -f %C
-H . --hintro=%h --houtro=%o -fF %C
-H . --hintro=%h --houtro=%o -Q %C
-H . --hintro=%h --houtro=%o --du %C
-H . --hintro=%h --houtro=%o -h --du %C
-H . --hintro=%h --houtro=%o %M %C
-H . --hintro=%h --houtro=%o /no/such/path-xyz
--fromfile %F/paths.txt
-F --fromfile %F/paths.txt
-a --fromfile %F/paths.txt
--dirsfirst --fromfile %F/paths.txt
-r --fromfile %F/paths.txt
-P *.c --fromfile %F/paths.txt
-P src --fromfile %F/paths.txt
--prune -P *.c --fromfile %F/paths.txt
-I *.log --fromfile %F/paths.txt
-d --fromfile %F/paths.txt
-pugsD --fromfile %F/paths.txt
-p --fromfile %F/paths.txt
--du --fromfile %F/paths.txt
-h --du --fromfile %F/paths.txt
-f --fromfile %F/paths.txt
-Q --fromfile %F/paths.txt
--fflinks --fromfile %F/links.txt
-J --fromfile %F/paths.txt
-X --fromfile %F/paths.txt
-H . --hintro=%h --houtro=%o --fromfile %F/paths.txt
--fromfile /no/such/file-xyz.txt
--fromtabfile %F/tabs.txt
--fflinks --fromtabfile %F/tabs.txt
--dirsfirst --fromtabfile %F/tabs.txt
--fromtabfile %F/tab_orphan.txt
-J --fromtabfile %F/tabs.txt
-X --fromtabfile %F/tabs.txt
%E
-d %E
-L 100 %E
-f %E
--du %E
-J %E
-X %E
--noreport %E
-L 0 %C
-L 0x2 %C
-L 010 %C
-L 5x %C
-L abc %C
-L
/no/such/path-xyz'

normprog() { sed 's/^tree: /PROG: /; s/^aspen: /PROG: /'; }

# Canonicalize documented intentional deviations (.docs/deviations.md) on BOTH
# tools' stdout, so a case that merely TOUCHES a deviation still gates everything
# else byte-for-byte. Applied to ref-vs-ref too (a no-op there). Only the exact
# bug shapes are rewritten, so it can't mask a real difference:
#   D3 — tree's signed-char %02X sign-extends a high byte to %FFFFFFC3; we and
#        the canonical form use %C3.  D2 — tree leaks an empty ,"contents":[ ]
#        after an error; valid JSON has no such key.
normdev() { sed 's/%FFFFFF\([0-9A-Fa-f][0-9A-Fa-f]\)/%\1/g; s/,"contents":\[ *\]//g'; }

run_case() { # <bin> <case-string>
	_bin=$1
	_expanded=$(printf '%s' "$2" | sed "s#%C#$corpus#g; s#%W#$weird#g; s#%L#$lnk#g; s#%M#$meta#g; s#%V#$vert#g; s#%P#$prn#g; s#%G#$gign#g; s#%I#$inf#g; s#%h#$work/hintro#g; s#%o#$work/houtro#g; s#%F#$ff#g; s#%E#$dep#g; s#%R#$rts#g; s#%D#$cnd#g; s#%O#$work/ofile#g")
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

# phase: compare $1 vs $2 over all CASES in the current locale. Writes the diff
# count to $work/phase.fails (a file, not the exit status, which caps at 256 and
# can't accumulate). DIFF lines go to stdout (visible — no longer /dev/null'd).
phase() { # <bin_a> <bin_b> <label>
	_a=$1; _b=$2; _label=$3; _fails=0; _n=0
	_oifs=$IFS
	IFS='
'
	for _c in $CASES; do
		[ -n "$_c" ] || continue
		_n=$((_n + 1))
		run_case "$_a" "$_c"
		normdev <"$work/o.out" >"$work/a.out"; normprog <"$work/o.err" >"$work/a.err"; cp "$work/o.rc" "$work/a.rc"
		run_case "$_b" "$_c"
		normdev <"$work/o.out" >"$work/b.out"; normprog <"$work/o.err" >"$work/b.err"; cp "$work/o.rc" "$work/b.rc"
		if ! diff -q "$work/a.out" "$work/b.out" >/dev/null 2>&1 ||
		   ! diff -q "$work/a.err" "$work/b.err" >/dev/null 2>&1 ||
		   [ "$(cat "$work/a.rc")" != "$(cat "$work/b.rc")" ]; then
			_fails=$((_fails + 1))
			echo "  DIFF [$_label/${LC_ALL:-C}]: $_c"
		fi
	done
	IFS=$_oifs
	echo "$_label [${LC_ALL:-C}]: $_n cases, $_fails diffs" >&2
	echo "$_fails" > "$work/phase.fails"
}

# Run the whole suite under C (byte-order collation) and, when available, a
# dictionary UTF-8 locale (exercises strxfrm sort + multibyte name printing).
# Skip a UTF-8 locale gracefully where none is installed (e.g. musl/Alpine).
utf8=""
for _L in en_US.UTF-8 en_US.utf8; do
	[ "$(LC_ALL=$_L locale charmap 2>/dev/null)" = "UTF-8" ] && { utf8=$_L; break; }
done
# Also a byte-order UTF-8 locale (C.UTF-8): exercises SR-3.2 — the strxfrm-identity
# collation probe (strcmp sort path) and the ASCII name fast path — which the
# dictionary locale above does not. Added when present and distinct.
cutf8=""
for _L in C.UTF-8 C.utf8; do
	[ "$(LC_ALL=$_L locale charmap 2>/dev/null)" = "UTF-8" ] && { cutf8=$_L; break; }
done
locales="C"
[ -n "$utf8" ] && locales="$locales $utf8"
[ -n "$cutf8" ] && [ "$cutf8" != "$utf8" ] && locales="$locales $cutf8"
echo "GOLDEN: locales = $locales"

selffail=0; parityfail=0
for _lc in $locales; do
	export LC_ALL="$_lc"
	phase "$ref" "$ref" "self-test(ref-vs-ref)"
	selffail=$((selffail + $(cat "$work/phase.fails")))
	if [ -f "$here/PARITY_ACTIVE" ] && [ -x "$ASP" ]; then
		phase "$ASP" "$ref" "parity(aspen-vs-ref)"
		parityfail=$((parityfail + $(cat "$work/phase.fails")))
	fi
	unset LC_ALL
done

rc=0
if [ "$selffail" -ne 0 ]; then
	echo "GOLDEN: self-test FAILED ($selffail) — harness or corpus is non-deterministic"
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
