#!/bin/sh
# Line coverage of the aspen binary over the whole test suite (SR-1.12). Builds a
# specially-instrumented aspen, runs tests/run.sh against it (golden + fuzzer +
# deviations + structural all exercise ./aspen), and reports per-file coverage.
#
# Two backends, picked from $CC: clang -> source-based (-fprofile-instr-generate
# /-fcoverage-mapping + llvm-cov, the cleanest report); gcc -> --coverage + gcovr
# (or raw gcov if gcovr is absent). Unit tests link their own objects, so this
# measures the shipped binary's behaviour, which is what parity/perf depend on.
set -u
CC=${CC:-cc}
work=tests/.work
cov="$work/cov"
rm -rf "$cov"; mkdir -p "$cov"

# Library sources that make up the binary (mirror the Makefile; exclude main only
# for the report's denominator if desired — here we include everything compiled).
SRCS=$(ls src/*.c src/sys/*.c src/render/*.c)

is_clang=0
$CC --version 2>&1 | grep -qi clang && is_clang=1

echo "COVERAGE: CC=$CC backend=$([ $is_clang = 1 ] && echo clang-source || echo gcov)"

if [ "$is_clang" = 1 ]; then
	# Build instrumented (separate object dir not needed; reuse normal layout).
	gmake -s clean
	gmake -s aspen asp \
		CFLAGS="-O0 -g -fprofile-instr-generate -fcoverage-mapping" \
		LDFLAGS="-fprofile-instr-generate -fcoverage-mapping" || {
		echo "COVERAGE: instrumented build failed"; exit 1; }

	# The suite spawns ./aspen hundreds of times, sequentially. %p (pid) would
	# TRUNCATE-on-write and lose data whenever a pid is reused; %m uses an online
	# merge pool (file-locked, append-merged) so every run accumulates.
	LLVM_PROFILE_FILE="$cov/aspen-%m.profraw" ASP_TEST_SANITIZE=0 FUZZ_N=${FUZZ_N:-40} \
		sh tests/run.sh >/dev/null 2>&1 || echo "COVERAGE: (suite reported failures; coverage still computed)"

	set -- "$cov"/*.profraw
	[ -e "$1" ] || { echo "COVERAGE: no .profraw produced"; exit 1; }
	llvm-profdata merge -sparse "$cov"/*.profraw -o "$cov/aspen.profdata" || exit 1
	# -object names the binary; with no source args llvm-cov reports every file in
	# the coverage map. Highlight the least-covered files at the end.
	llvm-cov report -instr-profile="$cov/aspen.profdata" -object ./aspen
	echo "COVERAGE: annotate a file -> llvm-cov show -object ./aspen -instr-profile=$cov/aspen.profdata src/<f>.c"
else
	gmake -s clean
	gmake -s aspen asp CFLAGS="-O0 -g --coverage" LDFLAGS="--coverage" || {
		echo "COVERAGE: instrumented build failed"; exit 1; }
	ASP_TEST_SANITIZE=0 FUZZ_N=${FUZZ_N:-40} sh tests/run.sh >/dev/null 2>&1 ||
		echo "COVERAGE: (suite reported failures; coverage still computed)"
	if command -v gcovr >/dev/null 2>&1; then
		gcovr -r . --txt -e 'tests/.*' -e 'src/iouring\.c'
	else
		echo "COVERAGE: gcovr not found; running gcov (raw .gcov files in $cov)"
		(cd "$cov" && gcov -r -o ../../../src $SRCS >/dev/null 2>&1) || true
		gcov -n -o src $SRCS 2>/dev/null | grep -A1 "File 'src" | grep -E "File|Lines exec" || true
	fi
fi

# leave the build in a coverage state? no — restore a normal build for the dev.
gmake -s clean >/dev/null 2>&1 || true
gmake -s >/dev/null 2>&1 || true
echo "COVERAGE: done (normal build restored)"
