#!/bin/sh
# -R (rerun) parity: at each -L boundary tree re-renders the subtree into a
# generated <dir>/00Tree.html file (format-agnostic in tree's list.c). aspen does
# the same. Because -R WRITES into the corpus, each tool gets its OWN copy so the
# files one writes never leak into the other's read. We compare stdout, the SET of
# generated files, AND their contents — the whole point of -R is the files, which
# the stdout-only golden matrix can't see.
#
# Normalization: the only legitimate differences are the program identity (aspen
# vs tree in the HTML header/footer/version) — exactly what the main -H golden
# cases already normalize. We strip the HTML head (through <body>) and the VERSION
# footer (from <hr>) so the comparison is over the tree body + report only.
#
# At -L >= 2 the nested file BODIES differ by DEVIATION D6 (tree leaks its global
# dirs[] indent state into the nested document); that is asserted in deviations.sh.
# Here we assert the -L1 case is byte-identical (stdout + files), and that at -L2
# the stdout, the generated file SET, and the per-file directory/file COUNTS match.
set -u
work=tests/.work
root=$(cd "$(dirname "$0")/../.." && pwd)
ref="$root/$work/ref/tree-2.3.2"   # absolute: the corpus runs cd into a subdir
ASP="$root/aspen"
rr="$root/$work/rerun"
fail=0

[ -x "$ASP" ] || { echo "RERUN: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null 2>&1 || { echo "RERUN: no ref"; exit 1; }

mkdir -p "$rr"

# strip HTML identity (head + version footer); leave the <body> tree + report.
strip_html() { awk '/<body>/{p=1;next} /<hr>/{p=0} p' "$1"; }

mkfixture() { # $1 = dest dir
	rm -rf "$1"
	mkdir -p "$1/a/aa/aaa" "$1/a/ab" "$1/b/ba" "$1/c"
	: > "$1/a/f1"; : > "$1/a/aa/f2"; : > "$1/ztop"
}

run_both() { # flags...  -> $rr/a.out,$rr/r.out and corpora $rr/A,$rr/R
	mkfixture "$rr/A"; mkfixture "$rr/R"
	( cd "$rr/A" && "$ASP" "$@" . ) >"$rr/a.out" 2>"$rr/a.err"
	( cd "$rr/R" && "$ref" "$@" . ) >"$rr/r.out" 2>"$rr/r.err"
}

filelist() { ( cd "$1" && find . -name 00Tree.html | LC_ALL=C sort ); }

# compare two files' HTML bodies (stripped); $3 = label
cmp_html() {
	strip_html "$1" >"$rr/_r"; strip_html "$2" >"$rr/_a"
	if ! diff "$rr/_r" "$rr/_a" >/dev/null; then
		echo "RERUN[$3]: differs from tree"; diff "$rr/_r" "$rr/_a" | sed -n '1,8p'; fail=1
	fi
}

cmp_fileset() { # $1=ref corpus $2=aspen corpus $3=label
	filelist "$1" >"$rr/_rf"; filelist "$2" >"$rr/_af"
	if ! diff "$rr/_rf" "$rr/_af" >/dev/null; then
		echo "RERUN[$3]: generated 00Tree.html file SET differs"; diff "$rr/_rf" "$rr/_af" | sed -n '1,8p'; fail=1
	fi
}

# ---- Case 1: -H -R -L1 — full byte parity (stdout + file set + file bodies) ----
run_both -H HOST -R -L 1
cmp_html "$rr/r.out" "$rr/a.out" "-H -R -L1 stdout"
cmp_fileset "$rr/R" "$rr/A" "-H -R -L1"
filelist "$rr/R" >"$rr/_rf"
while IFS= read -r f; do
	[ -f "$rr/A/$f" ] || { echo "RERUN[-H -R -L1]: missing $f"; fail=1; continue; }
	cmp_html "$rr/R/$f" "$rr/A/$f" "-H -R -L1 file $f"
done <"$rr/_rf"

# ---- Case 2: text -R -L1 — full byte parity (text content into 00Tree.html) ----
run_both -R -L 1
if ! diff "$rr/r.out" "$rr/a.out" >/dev/null; then
	echo "RERUN[text -R -L1]: stdout differs from tree"; diff "$rr/r.out" "$rr/a.out" | sed -n '1,8p'; fail=1
fi
cmp_fileset "$rr/R" "$rr/A" "text -R -L1"
filelist "$rr/R" >"$rr/_rf"
while IFS= read -r f; do
	if ! diff "$rr/R/$f" "$rr/A/$f" >/dev/null 2>&1; then
		echo "RERUN[text -R -L1]: body of $f differs from tree"; diff "$rr/R/$f" "$rr/A/$f" | sed -n '1,8p'; fail=1
	fi
done <"$rr/_rf"

# ---- Case 3: -H -R -L2 — stdout + file SET + per-file COUNTS match (bodies D6) ----
run_both -H HOST -R -L 2
cmp_html "$rr/r.out" "$rr/a.out" "-H -R -L2 stdout"
cmp_fileset "$rr/R" "$rr/A" "-H -R -L2"
countline() { grep -E '[0-9]+ director' "$1" | tail -1; }
filelist "$rr/R" >"$rr/_rf"
while IFS= read -r f; do
	cr=$(countline "$rr/R/$f"); ca=$(countline "$rr/A/$f")
	if [ "$cr" != "$ca" ]; then
		echo "RERUN[-H -R -L2]: count line of $f differs: tree[$cr] aspen[$ca]"; fail=1
	fi
done <"$rr/_rf"

chmod -R u+rwx "$rr" 2>/dev/null || :
rm -rf "$rr"

if [ "$fail" -eq 0 ]; then
	echo "RERUN: ok (-R 00Tree.html generation byte-identical at -L1; stdout+fileset+counts at -L2)"
else
	exit 1
fi
