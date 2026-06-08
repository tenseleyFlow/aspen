#!/bin/sh
# Cross-directory recursive-symlink cycle detection (SR02-0.2). tree sorts each
# directory level THEN walks, so -l cycle detection resolves a cross-dir loop
# against siblings in sorted order. aspen's full-tree path (build_level, used by
# -J/-X/--du) once detected cycles in raw readdir order, making the descent
# decision filesystem/inode-dependent — nondeterministic 7/4-vs-5/2 dir/file
# counts. Fixed by sorting in build_level before the descent loop (like walk_dir).
#
# This bug only fires for certain inode layouts, so we RE-CREATE the fixture many
# times (fresh inodes each time) and assert aspen == tree every time. We check the
# -l text output in full, and the -J/-X report COUNTS (their full output still
# differs by the separate M1 recursive-link indent item, SR02-0.3, until that
# lands — at which point this should be upgraded to a full -J/-X compare).
set -u
work=tests/.work
ref="$work/ref/tree-2.3.2"
ASP=$(cd "$(dirname "$0")/../.." && pwd)/aspen
cy="$work/cyclic"
N=${CYCLIC_N:-30}
fail=0

[ -x "$ASP" ] || { echo "CYCLIC: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null 2>&1 || { echo "CYCLIC: no ref"; exit 1; }

mkfix() {
	rm -rf "$cy"
	mkdir -p "$cy/a/sub" "$cy/b/sub" "$cy/c"
	: > "$cy/a/sub/leaf"; : > "$cy/b/f"
	ln -s ../..   "$cy/a/sub/up"    # self-loop back to root
	ln -s ../../a "$cy/b/sub/toa"   # cross-dir: b/sub -> a
	ln -s ../b    "$cy/a/tob"       # cross-dir: a -> b
}

count_json() { grep -o '"directories":[0-9]*,"files":[0-9]*'; }
count_xml()  { grep -o 'directories="[0-9]*" files="[0-9]*"'; }

i=0
while [ "$i" -lt "$N" ]; do
	i=$((i + 1))
	mkfix
	# -l text: full byte parity (this path was always correct; guards a regression)
	"$ref" -l "$cy" >"$cy/.r" 2>&1; "$ASP" -l "$cy" >"$cy/.a" 2>&1
	if ! diff "$cy/.r" "$cy/.a" >/dev/null; then
		echo "CYCLIC[-l iter $i]: text output differs from tree"; diff "$cy/.r" "$cy/.a" | sed -n '1,10p'; fail=1; break
	fi
	# -J / -X: report counts must match (cycle decision == tree's)
	rj=$("$ref" -l -J "$cy" 2>/dev/null | count_json); aj=$("$ASP" -l -J "$cy" 2>/dev/null | count_json)
	if [ "$rj" != "$aj" ]; then
		echo "CYCLIC[-J iter $i]: report counts differ tree[$rj] aspen[$aj]"; fail=1; break
	fi
	rx=$("$ref" -l -X "$cy" 2>/dev/null | count_xml); ax=$("$ASP" -l -X "$cy" 2>/dev/null | count_xml)
	if [ "$rx" != "$ax" ]; then
		echo "CYCLIC[-X iter $i]: report counts differ tree[$rx] aspen[$ax]"; fail=1; break
	fi
	# determinism: aspen must agree with itself across the pool and serial paths
	as=$("$ASP" --threads 1 -l -J "$cy" 2>/dev/null | count_json)
	if [ "$as" != "$aj" ]; then
		echo "CYCLIC[iter $i]: aspen serial[$as] != pool[$aj] (nondeterministic)"; fail=1; break
	fi
done

rm -rf "$cy"

if [ "$fail" -eq 0 ]; then
	echo "CYCLIC: ok (cross-dir cycle detection deterministic + == tree, $N inode layouts)"
else
	exit 1
fi
