#!/bin/sh
# Cross-directory recursive-symlink cycle detection (SR02-0.2). tree sorts each
# directory level THEN walks, so -l cycle detection resolves a cross-dir loop
# against siblings in sorted order. aspen's full-tree path (build_level, used by
# -J/-X/--du) once detected cycles in raw readdir order, making the descent
# decision filesystem/inode-dependent — nondeterministic 7/4-vs-5/2 dir/file
# counts. Fixed by sorting in build_level before the descent loop (like walk_dir).
#
# This bug only fires for certain inode layouts, so we RE-CREATE the fixture many
# times (fresh inodes each time) and assert aspen == tree every time. We compare
# the full output of -l, -J and -X (SR02-0.3 / M1 made the -J/-X recursive-link
# close byte-identical too, so a full compare now holds).
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

i=0
while [ "$i" -lt "$N" ]; do
	i=$((i + 1))
	mkfix
	# full byte parity for -l (text), -J and -X across each inode layout
	for fmt in "" "-J" "-X"; do
		"$ref" -l $fmt "$cy" >"$cy/.r" 2>&1; "$ASP" -l $fmt "$cy" >"$cy/.a" 2>&1
		if ! diff "$cy/.r" "$cy/.a" >/dev/null; then
			echo "CYCLIC[-l ${fmt:-text} iter $i]: differs from tree"; diff "$cy/.r" "$cy/.a" | sed -n '1,10p'; fail=1; break 2
		fi
	done
	# determinism: aspen's pool and serial paths must agree (the bug was order-dependent)
	aj=$("$ASP" -l -J "$cy" 2>/dev/null | count_json)
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
