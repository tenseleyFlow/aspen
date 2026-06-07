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

# UTF-8 locale (default sort is strcoll, so timing must hold under both); skip if
# none installed (musl/Alpine). Benchmark under C and, when present, UTF-8.
utf8=""
for L in en_US.UTF-8 en_US.utf8 C.UTF-8 C.utf8; do
	[ "$(LC_ALL=$L locale charmap 2>/dev/null)" = "UTF-8" ] && { utf8=$L; break; }
done
locales="C"; [ -n "$utf8" ] && locales="C $utf8"

# Gate one (config, locale) pair. Gate on the mean when tree's run is above the
# ~5ms timing-noise floor of shared CI runners; below it, gate on the MIN (the
# least-perturbed run, a stable proxy for true compute cost) — so the startup-
# dominated -L1/-L2 cases are GATED, not skipped.
bench_pair() { # <label> <flags-string>
	_lab=$1; _flags=$2
	for _lc in $locales; do
		_csv="$work/m_${_lab}_${_lc}.csv"
		if ! LC_ALL="$_lc" hyperfine -N -w 5 -r 30 --export-csv "$_csv" \
			"$ASP $_flags $corpus" "$REF -n $_flags $corpus" >/dev/null 2>&1; then
			echo "bench: hyperfine failed for $_lab/$_lc"; continue
		fi
		_tmean=$(awk -F, 'NR>1 { split($1,w," "); if (w[1] ~ /tree/) {print $2; exit} }' "$_csv")
		if awk -v t="$_tmean" 'BEGIN { exit !(t + 0 < 0.005) }'; then
			sh bench/gate.sh "$_csv" min "$_lab/$_lc" || rc=1
		else
			sh bench/gate.sh "$_csv" mean "$_lab/$_lc" || rc=1
		fi
	done
}

if [ -f tests/golden/PARITY_ACTIVE ] && [ -x "$ASP" ]; then
	echo "== perf: aspen vs tree ($REF), warm cache, output -> discarded, locales: $locales =="
	# Flag matrix — only configs the golden suite certified byte-identical to
	# tree (so the speed number is honest). NO -H/-T: HTML embeds aspen's own
	# name/version, so that output is intentionally not byte-identical.
	bench_pair default ""
	bench_pair s   "-s"
	bench_pair a   "-a"
	bench_pair U   "-U"
	bench_pair J   "-J"
	bench_pair X   "-X"
	bench_pair C   "-C"
	bench_pair L1  "-L 1"
	bench_pair L2  "-L 2"

	# Startup canary: --version is pure process startup (no walk). REPORTED, not
	# gated — once the libthr lazy-load landed aspen's startup ~= tree's (a tie),
	# so strict gating would flap on CI noise; the real libthr-regression guard is
	# structural (dlopen, no -lpthread) plus the strict-min -L1 gate above.
	if hyperfine -N -w 10 -r 100 --export-csv "$work/m_version.csv" \
		"$ASP --version" "$REF --version" >/dev/null 2>&1; then
		sh bench/gate.sh "$work/m_version.csv" min "version-startup(report)" || true
	fi

	# Per-shape gate: flat (sort+output), wide (large tree), deep (syscall-bound
	# chain). Built on demand; ASP_BENCH_CLASSES=0 skips (constrained CI).
	if [ "${ASP_BENCH_CLASSES:-1}" != 0 ]; then
		sh bench/mkclasses.sh "$work" >/dev/null 2>&1 || true
		for cls in flat wide deep; do
			[ -d "$work/$cls" ] || continue
			csv="$work/cls-$cls.csv"
			hyperfine -N -w 5 -r 30 --export-csv "$csv" \
				"$ASP $work/$cls" "$REF -n $work/$cls" >/dev/null 2>&1 || continue
			tmean=$(awk -F, 'NR>1 { split($1,w," "); if (w[1] ~ /tree/) {print $2; exit} }' "$csv")
			if awk -v t="$tmean" 'BEGIN { exit !(t + 0 < 0.005) }'; then
				sh bench/gate.sh "$csv" min "class:$cls" || rc=1
			else
				sh bench/gate.sh "$csv" mean "class:$cls" || rc=1
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
