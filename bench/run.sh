#!/bin/sh
# Benchmark aspen vs tree (warm cache, no color, same sink). Fair build comparison:
# tree uses its own -O3 build; aspen uses a portable-baseline release (no -march=native).
# The gate activates once tests/golden/PARITY_ACTIVE exists (Sprint 02) — we only
# claim speed on output the golden suite proved byte-identical.
set -u

work=bench/.work
mkdir -p "$work"
corpus="$work/corpus"
ASP=${ASP:-./aspen}
REF232=tests/.work/ref/tree-2.3.2
# Benchmark against the exact parity target (tree 2.3.2) the golden suite builds:
# version-precise and present everywhere. A system `tree` is absent on macOS CI
# (and unknown-version elsewhere). Override REF= to compare against another build.
[ -x "$REF232" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null 2>&1 || true
REF=${REF:-$REF232}

[ -d "$corpus" ] || sh bench/mkcorpus.sh "$corpus" >/dev/null

command -v hyperfine >/dev/null 2>&1 || {
	echo "bench: hyperfine not found — install it to benchmark. Skipping."
	exit 0
}

rc=0
if [ -f tests/golden/PARITY_ACTIVE ] && [ -x "$ASP" ]; then
	echo "== perf: aspen vs tree ($REF), warm cache, output -> discarded =="
	hyperfine -N -w 3 -r 10 --export-csv "$work/results.csv" \
		"$ASP $corpus" "$REF -n $corpus"
	sh bench/gate.sh "$work/results.csv" || rc=1

	# Per-shape gate: flat (sort+output), wide (common large tree), deep
	# (syscall-bound chain). Each must still beat tree. Built on demand;
	# ASP_BENCH_CLASSES=0 skips (e.g. constrained CI).
	if [ "${ASP_BENCH_CLASSES:-1}" != 0 ]; then
		sh bench/mkclasses.sh "$work" >/dev/null 2>&1 || true
		for cls in flat wide deep; do
			[ -d "$work/$cls" ] || continue
			echo "== perf class: $cls =="
			hyperfine -N -w 2 -r 8 --export-csv "$work/cls-$cls.csv" \
				"$ASP $work/$cls" "$REF -n $work/$cls" >/dev/null
			# Sub-5ms workloads are unreliable on shared CI runners (deep is a
			# PATH_MAX-bounded chain that can't grow); report but don't gate them
			# — gating only the comparisons that are above timing noise.
			tmean=$(awk -F, 'NR>1 && /tree/ {print $2; exit}' "$work/cls-$cls.csv")
			if awk -v t="$tmean" 'BEGIN { exit !(t + 0 < 0.005) }'; then
				sh bench/gate.sh "$work/cls-$cls.csv" || true
				echo "   (tree ${tmean}s < 5ms noise floor — reported, not gated)"
			else
				sh bench/gate.sh "$work/cls-$cls.csv" || rc=1
			fi
		done
	fi
else
	echo "== perf harness demo (aspen not producing real output yet) =="
	echo "Proving the harness/gate work by comparing two reference builds:"
	[ -x "$REF232" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null 2>&1 || true
	if [ -x "$REF232" ]; then
		hyperfine -N -w 3 -r 10 "$REF -n $corpus" "$REF232 -n $corpus" || true
	else
		hyperfine -N -w 3 -r 10 "$REF -n $corpus" || true
	fi
	echo "(perf gate inactive until Sprint 02 creates tests/golden/PARITY_ACTIVE)"
fi
exit $rc
