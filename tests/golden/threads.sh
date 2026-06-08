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

# Fixture: one wide level (500 files of mixed names) + a subdir with 80 files +
# a symlink. 500 > ASP_STAT_PAR_MIN (384) so the pool actually fires; symlinks/-F
# paths covered too.
if [ ! -d "$big" ]; then
	mkdir -p "$big/sub"
	i=0
	while [ "$i" -lt 500 ]; do
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

# A3 dead-zone guard: a directory BELOW ASP_STAT_PAR_MIN under a stat flag must NOT
# spawn the worker pool — spawning it there loses to tree (the default config was
# ~1.3x slower on `-s ~/project`, the most common stat workload). Probe aspen's OWN
# pool decision via ASP_TRACE_POOL (a one-line stderr report emitted exactly when the
# lazy pool is created). This is deterministic and host-independent — unlike counting
# thread-creation syscalls, which can't distinguish the pool from libc/jemalloc
# startup threads (those vary per host and made a raw-syscall delta flaky on CI).
# Pin ASP_IO=threads (the documented pool selector) so this tests the POOL threshold
# regardless of the ambient backend — under the ASP_IO=uring CI step the pool would
# otherwise never spawn (io_uring backend) and the probe would be vacuous/wrong.
dz="$work/threaddz"; rm -rf "$dz"; mkdir -p "$dz"
i=0; while [ "$i" -lt 150 ]; do : > "$dz/f$i"; i=$((i + 1)); done
# Auto worker-count path: 150 < threshold, so the pool must stay serial.
if ASP_IO=threads ASP_TRACE_POOL=1 "$ASP" -s "$dz" 2>&1 >/dev/null | grep -q "stat-pool spawned"; then
	echo "THREADS: -s spawned the pool on a 150-file dir (< ASP_STAT_PAR_MIN dead zone; loses to tree — directive #2, audit A3)"; fail=1
fi
# Even with a pool explicitly requested, the 384 threshold must still gate 150 files
# (--threads forces workers>1 regardless of this host's CPU count, so the check is
# the threshold alone, not the auto worker-count decision).
if ASP_IO=threads ASP_TRACE_POOL=1 "$ASP" --threads 8 -s "$dz" 2>&1 >/dev/null | grep -q "stat-pool spawned"; then
	echo "THREADS: --threads 8 -s spawned the pool on a 150-file dir — the ASP_STAT_PAR_MIN threshold was bypassed (audit A3)"; fail=1
fi
# Positive control: above the threshold the lazy spawn MUST fire (forced threads, so
# this is deterministic on a 1-CPU runner too) — proves the trace + spawn both work.
# Use a dedicated dir sized well over ASP_STAT_PAR_MIN so the control never depends on
# the shared fixture's (drifting) entry count.
up="$work/threadup"; rm -rf "$up"; mkdir -p "$up"
i=0; while [ "$i" -lt 500 ]; do : > "$up/f$i"; i=$((i + 1)); done
if ! ASP_IO=threads ASP_TRACE_POOL=1 "$ASP" --threads 8 -s "$up" 2>&1 >/dev/null | grep -q "stat-pool spawned"; then
	echo "THREADS: --threads 8 -s did NOT spawn the pool on a 500-file dir (> threshold — the lazy spawn should fire here)"; fail=1
fi
rm -rf "$dz" "$up"

if [ "$fail" -eq 0 ]; then
	echo "THREADS: parallel stat deterministic (--threads 1 == 16 == tree); strict parse; dead-zone serial"
else
	exit 1
fi
