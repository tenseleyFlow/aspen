#!/bin/sh
# Parallel-stat determinism + parity (Sprint 11b). The metadata stat pass farms
# fstatat() to a worker pool for directory levels above a threshold; output is
# decided by name before any stat, so it MUST be byte-identical regardless of
# thread count. The golden corpora have small levels (pool never fires), so this
# builds a level large enough to exercise the pool and checks, for every
# stat-heavy mode, that --threads 1 == --threads 16 == tree, in both locales.
set -u

work=tests/.work
ref="$work/ref/tree-2.3.2"
ASP=./aspen
big="$work/threadbig"
fail=0

[ -x "$ASP" ] || { echo "THREADS: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null || { echo "THREADS: no ref"; exit 1; }

# Fixture: one wide level (300 files of mixed names) + a subdir with 80 files +
# a symlink, so the pool fires (>= threshold) and symlinks/-F paths are covered.
if [ ! -d "$big" ]; then
	mkdir -p "$big/sub"
	i=0
	while [ "$i" -lt 300 ]; do
		: > "$big/f$(printf %04d "$i").dat"
		i=$((i + 1))
	done
	i=0
	while [ "$i" -lt 80 ]; do
		: > "$big/sub/s$(printf %03d "$i").txt"
		i=$((i + 1))
	done
	ln -sf f0000.dat "$big/link"
fi

chk() { # <desc> <args...>
	_d=$1; shift
	for _lc in C en_US.UTF-8; do
		LC_ALL=$_lc "$ref" -n "$@" "$big" >"$work/th.t" 2>/dev/null
		LC_ALL=$_lc "$ASP" --threads 1 "$@" "$big" >"$work/th.s" 2>/dev/null
		LC_ALL=$_lc "$ASP" --threads 16 "$@" "$big" >"$work/th.p" 2>/dev/null
		if ! diff -q "$work/th.t" "$work/th.s" >/dev/null 2>&1 ||
		   ! diff -q "$work/th.s" "$work/th.p" >/dev/null 2>&1; then
			echo "THREADS DIFF [$_d, $_lc]"
			{ diff "$work/th.s" "$work/th.p" || diff "$work/th.t" "$work/th.p"; } | sed -n '1,6p' | cat -v
			fail=1
		fi
	done
}

chk "-s"        -s
chk "--du"      --du
chk "-pugsD"    -p -u -g -s -D
chk "-C"        -C
chk "-t (mtime sort)" -t
chk "-F"        -F
chk "-s -P"     -s -P 'f01*'
chk "--du -J"   --du -J
chk "-s -X"     -s -X

# SR02-2.8: --threads is aspen-only, so we parse it strictly — junk/garbage/out-of-
# range is rejected (exit 1), unlike atoi's silent 0. 0 = auto; 1/8 valid.
for bad in abc -1 1x 999999 ""; do
	if "$ASP" --threads "$bad" "$big" >/dev/null 2>&1; then
		echo "THREADS: --threads '$bad' accepted (should reject)"; fail=1
	fi
done
for ok in 0 1 8; do
	if ! "$ASP" --threads "$ok" "$big" >/dev/null 2>&1; then
		echo "THREADS: --threads '$ok' rejected (should accept)"; fail=1
	fi
done

if [ "$fail" -eq 0 ]; then
	echo "THREADS: parallel stat is deterministic (--threads 1 == 16 == tree); strict parse"
else
	exit 1
fi
