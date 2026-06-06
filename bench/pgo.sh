#!/bin/sh
# Profile-guided optimization build (opt-in; clang/llvm only).
#
# Two passes: instrument, gather profiles over the DIVERSE bench corpora (flat,
# wide, deep, mixed) in both locales — never one shape (a non-representative
# profile pessimizes real use) — then rebuild with -fprofile-use.
#
# Measured gain on this workload is marginal (~5% on large dirs, within noise on
# others), so the default `make release` stays plain for reproducible packaged
# builds. Use this only for a local, self-tuned binary.
set -eu

work=bench/.work
pgo="$work/pgo"
prof="$work/pgo.profdata"
mk=${MAKE:-gmake}

command -v llvm-profdata >/dev/null 2>&1 || {
	echo "pgo: llvm-profdata not found (clang/llvm toolchain required)"; exit 1; }

# Diverse corpora — build the ones the profiling pass exercises.
sh bench/mkcorpus.sh "$work/corpus" >/dev/null
[ -d "$work/flat" ] || { echo "pgo: build bench/.work/{flat,wide,deep} first (see bench notes)"; }

rm -rf "$pgo"; mkdir -p "$pgo"

echo "pgo: pass 1 — instrumented build"
$mk clean >/dev/null
$mk all CFLAGS="-O3 -DNDEBUG -fprofile-generate=$pgo" \
	LDFLAGS="-flto -fprofile-generate=$pgo" >/dev/null

echo "pgo: gathering profiles over diverse shapes, both locales"
for L in C en_US.UTF-8; do
	for c in flat wide deep corpus; do
		[ -d "$work/$c" ] || continue
		env LC_ALL="$L" ./aspen "$work/$c" >/dev/null 2>&1 || true
		env LC_ALL="$L" ./aspen -pugsD "$work/$c" >/dev/null 2>&1 || true
	done
done
llvm-profdata merge -output="$prof" "$pgo"/*.profraw

echo "pgo: pass 2 — profile-use rebuild"
$mk clean >/dev/null
$mk all CFLAGS="-O3 -DNDEBUG -fprofile-use=$prof -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled" \
	LDFLAGS="-flto -fprofile-use=$prof" >/dev/null

echo "pgo: done — ./aspen is a PGO build (profile: $prof)"
