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

# SR02-3.2: this script chmod-000's a fixture dir; clean it (and any leftover from
# a previously-interrupted run) on every exit incl. Ctrl-C, so the next run isn't
# blocked or falsely red by a permission-denied rm.
trap 'chmod -R u+rwx "$dv" 2>/dev/null; rm -rf "$dv"' EXIT INT TERM

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

# The unreadable-dir trigger needs a non-root euid (root bypasses 0000 perms, so
# opendir succeeds and there is legitimately no error — e.g. the FreeBSD CI VM
# runs as root). The filelimit trigger below is permission-independent and runs
# everywhere, so D1 is still asserted on every platform.
if [ "$(id -u 2>/dev/null || echo 0)" != 0 ]; then
	d1 "--du perm"         "$dv/perm" --du
	d1 "--prune perm"      "$dv/perm" --prune
	d1 "--matchdirs perm"  "$dv/perm" --matchdirs
	d1 "--du -J perm"      "$dv/perm" --du -J
	d1 "--condense perm"   "$dv/perm" --condense
	d1 "--condense -X perm" "$dv/perm" --condense -X
else
	echo "DEVIATIONS: running as root — skipping permission-based D1 cases (filelimit covers D1)"
fi
d1 "--filelimit+du"    "$dv/fl"   --filelimit 3 --du
d1 "--filelimit+prune" "$dv/fl"   --filelimit 3 --prune

# SR-2.12: the ROOT itself over --filelimit. Plain/-J/-X output is byte-identical
# to tree (d1 checks stdout==tree + rc 2). fl/big has 5 entries > limit 3.
d1 "root-overlimit"    "$dv/fl/big" --filelimit 3
d1 "root-overlimit -J" "$dv/fl/big" --filelimit 3 -J
d1 "root-overlimit -X" "$dv/fl/big" --filelimit 3 -X
# --du root variant: aspen stays consistent ("exceeds filelimit" + 1 dir);
# tree relabels it "error opening dir" + 1 file. Assert aspen's consistent form.
"$ASP" --du --filelimit 3 "$dv/fl/big" >"$dv/du.o" 2>/dev/null
grep -q 'exceeds filelimit' "$dv/du.o" || { echo "DEVIATIONS SR-2.12: --du root over-limit lost the consistent marker"; cat "$dv/du.o"; fail=1; }
grep -q '1 director' "$dv/du.o" || { echo "DEVIATIONS SR-2.12: --du root over-limit not counted as 1 directory"; cat "$dv/du.o"; fail=1; }

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

# D4: a symlink's --inodes/--device is the link's OWN, in -J/-X — not tree's
# target ino/dev (which is 0 for a broken link). brk -> nowhere is broken.
ln -s nowhere "$dv/brk"
"$ASP" -J --inodes --device "$dv" >"$dv/d4" 2>/dev/null
_brk=$(grep -oE '"type":"link","name":"brk"[^}]*' "$dv/d4")
case "$_brk" in
	*'"inode":0'*|*'"dev":0'*)
		echo "DEVIATIONS D4: broken-link -J inode/dev is 0 (regressed to tree's target stat): $_brk"; fail=1 ;;
	*'"inode":'*) : ;; # has a non-zero inode -> the link's own, good
	*) echo "DEVIATIONS D4: could not find brk link inode in -J output"; fail=1 ;;
esac

# D5: cleaner malformed-CLI parsing. Glued short-flag arg (-Pafoo) parses like
# -P afoo (getopt-style, no silent drop); exact long flags (--filelimit2 rejected).
mkdir -p "$dv/pq/sub"; : > "$dv/pq/afoo"; : > "$dv/pq/sub/b"
set -f
"$ASP" -Pafoo  "$dv/pq" >"$dv/d5g" 2>/dev/null
"$ASP" -P afoo "$dv/pq" >"$dv/d5s" 2>/dev/null
set +f
if ! diff -q "$dv/d5g" "$dv/d5s" >/dev/null 2>&1; then
	echo "DEVIATIONS D5: glued -Pafoo differs from -P afoo (silent-misparse regressed)"; fail=1
fi
"$ASP" --filelimit2 "$dv/pq" >/dev/null 2>&1 && { echo "DEVIATIONS D5: --filelimit2 accepted (should reject exact-match)"; fail=1; }

# D6: -R generated nested 00Tree.html files are self-indented correctly. tree
# leaks the parent walk's global dirs[] state into the nested document, drawing a
# guide (│) for an ancestor absent from that file and dropping the entry's own
# ├──/└── marker (visible at -L >= 2). aspen renders each 00Tree.html standalone.
# Fixture: a/{aa/{aaa,f2}, ab} — aa is a -L2 boundary WITH a sibling (ab), so
# tree's leftover dirs[] is non-zero and its bug fires.
r6="$dv/rr"; rm -rf "$r6"; mkdir -p "$r6/a/aa/aaa" "$r6/a/ab" "$r6/z"; : > "$r6/a/aa/f2"
asp_abs=$(cd "$(dirname "$ASP")" && pwd)/$(basename "$ASP")
( cd "$r6" && "$asp_abs" -H X -R -L 2 . >/dev/null 2>&1 )
nf6="$r6/a/aa/00Tree.html"
if [ ! -f "$nf6" ]; then
	echo "DEVIATIONS D6: -R did not generate the nested $nf6"; fail=1
elif ! grep -q '├' "$nf6"; then
	echo "DEVIATIONS D6: nested 00Tree.html lost its ├── branch marker (regressed to tree's dangling │ guide)"
	grep -nE 'aaa|f2' "$nf6" | sed -n '1,4p'; fail=1
fi

# D7: -l already-visited detection is deterministic (sorted) in every mode. tree's
# full-tree modes (-J/-X/--du/--condense/...) run the visited-check in raw readdir
# order, so a symlink to a real dir that also appears in the walk is followed or
# not depending on inode layout -> nondeterministic + inconsistent with tree's own
# streaming -l. aspen sorts in both engines, so full-tree -l == streaming -l == the
# sorted truth (= tree's streaming -l), every layout. Assert that here.
# pull the report's "<dirs> <files>" pair from any format (text or -J/-X).
nums() { grep -oE '[0-9]+ director[a-z]*, [0-9]+ file|directories":?="?[0-9]+|files":?="?[0-9]+|"directories":[0-9]+,"files":[0-9]+' | grep -oE '[0-9]+' | tr '\n' ' '; }
d7fail=0
k=0; while [ "$k" -lt 6 ]; do
	k=$((k + 1))
	r7="$dv/d7"; rm -rf "$r7"; mkdir -p "$r7/p" "$r7/realdir/sub"; : > "$r7/realdir/sub/f"
	ln -s ../realdir "$r7/p/link"
	truth=$("$ASP" -l "$r7" 2>/dev/null | nums)            # aspen streaming, sorted = the deterministic truth
	ref_s=$("$ref" -l "$r7" 2>/dev/null | nums)            # tree streaming (also sorted/deterministic)
	ac=$("$ASP" --condense -l "$r7" 2>/dev/null | nums)    # aspen full-tree text
	aj=$("$ASP" -J --condense -l "$r7" 2>/dev/null | nums) # aspen full-tree JSON
	if [ "$truth" != "$ref_s" ]; then
		echo "DEVIATIONS D7: aspen streaming -l[$truth] != tree streaming -l[$ref_s]"; d7fail=1
	fi
	if [ "$ac" != "$truth" ] || [ "$aj" != "$truth" ]; then
		echo "DEVIATIONS D7: aspen full-tree -l not self-consistent — stream[$truth] condense[$ac] -J[$aj]"; d7fail=1
	fi
	[ "$d7fail" = 1 ] && break
done
[ "$d7fail" = 1 ] && fail=1

# D8: -R is HTML/text-only. Under -J/-X aspen runs NO rerun — the -R output equals
# the no-R output (no spurious boundary "contents":[]) and no 00Tree.html files are
# written (tree writes JSON/XML-content files + emits the empty boundary contents;
# aspen does neither, consistent with D2).
r8="$dv/r8"; rm -rf "$r8"; mkdir -p "$r8/dir/child"
for fmt in -J -X; do
	"$ASP" $fmt -R -L 1 "$r8" >"$dv/r8.r" 2>/dev/null
	"$ASP" $fmt -L 1 "$r8" >"$dv/r8.n" 2>/dev/null
	if ! diff -q "$dv/r8.r" "$dv/r8.n" >/dev/null 2>&1; then
		echo "DEVIATIONS D8: $fmt -R changed the output (should be a no-op for JSON/XML)"; fail=1
	fi
done
nf8=$(find "$r8" -name 00Tree.html 2>/dev/null | wc -l)
if [ "$nf8" -ne 0 ]; then
	echo "DEVIATIONS D8: -J/-X -R wrote $nf8 00Tree.html file(s) (should be 0 — HTML/text only)"; fail=1
fi

# A7: the default -H VERSION footer is aspen's OWN identity line, not tree's 4-line
# copyright-attribution block (tree v… © Steve Baker / HTML+JSON+Charset credits).
# This is the sanctioned program-identity difference (deviations.md "Program
# identity"), but every golden -H case overrides the outtro with --houtro=%o, so the
# real default footer was never byte-checked. Assert aspen emits exactly its
# "\t\taspen v…" line and NONE of tree's attribution text — a regression that leaks
# "tree v"/"Steve Baker"/"copyleft", or drops our line, is then caught.
r7h="$dv/hfoot"; rm -rf "$r7h"; mkdir -p "$r7h/sub"; : > "$r7h/sub/f"
"$ASP" -H . "$r7h" >"$dv/hf.o" 2>/dev/null
if ! grep -q '<p class="VERSION">' "$dv/hf.o"; then
	echo "DEVIATIONS A7: default -H emitted no VERSION footer block"; fail=1
elif ! grep -Fq "$(printf '\t\taspen v')" "$dv/hf.o"; then
	echo "DEVIATIONS A7: default -H VERSION footer is not the '\\t\\taspen v…' identity line"
	grep -A1 'VERSION' "$dv/hf.o" | sed -n '1,3p' | cat -v; fail=1
elif grep -qiE 'tree v[0-9]|steve baker|copyleft' "$dv/hf.o"; then
	echo "DEVIATIONS A7: default -H footer leaked tree's attribution text (should be aspen identity only)"; fail=1
fi

chmod -R u+rwx "$dv" 2>/dev/null || :
rm -rf "$dv"

if [ "$fail" -eq 0 ]; then
	echo "DEVIATIONS: all documented deviations hold (aspen correct where tree bugs)"
else
	exit 1
fi
