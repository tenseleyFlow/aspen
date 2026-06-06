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
REF=${REF:-$(command -v tree 2>/dev/null || echo tree)}
REF232=tests/.work/ref/tree-2.3.2

[ -d "$corpus" ] || sh bench/mkcorpus.sh "$corpus" >/dev/null

command -v hyperfine >/dev/null 2>&1 || {
	echo "bench: hyperfine not found — install it to benchmark. Skipping."
	exit 0
}

if [ -f tests/golden/PARITY_ACTIVE ] && [ -x "$ASP" ]; then
	echo "== perf: aspen vs tree ($REF), warm cache, output -> discarded =="
	hyperfine -N -w 3 -r 10 --export-csv "$work/results.csv" \
		"$ASP $corpus" "$REF -n $corpus"
	sh bench/gate.sh "$work/results.csv"
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
