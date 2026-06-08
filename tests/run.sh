#!/bin/sh
# aspen test driver. Runs the unit tests, then the golden parity suite and the
# rest of tests/golden/ (fuzzer, deviations, flag/usage/structural lints, perf).
# Each unit test links against all library sources (everything in src/ except
# main.c) so cross-module deps resolve.
set -u

CC=${CC:-cc}
work=tests/.work
mkdir -p "$work"

[ -f config.mk ] || ./configure >/dev/null
# Reuse configure's per-platform feature macros (e.g. -D_GNU_SOURCE on Linux, so
# glibc exposes S_IFLNK/AT_FDCWD/syscall when the unit objects compile src/*.c).
conf_cflags=$(sed -n 's/^CONF_CFLAGS = //p' config.mk)
# ASan/UBSan by default; ASP_TEST_SANITIZE=0 disables them for toolchains that
# ship no sanitizer runtime (e.g. musl-gcc -> '__ubsan_handle_*: symbol not
# found' at load time). The golden parity suite still runs there.
san="-fsanitize=address,undefined -fno-sanitize-recover=all"
[ "${ASP_TEST_SANITIZE:-1}" = 0 ] && san=""
CFLAGS="-std=c11 -O1 -g $conf_cflags -Isrc -I. -Itests/unit -Wall -Wextra $san"
libsrc=$(ls src/*.c src/sys/*.c src/render/*.c 2>/dev/null | grep -v '/main\.c$')
# Optional link libs configure selected (e.g. -luring, -lpthread) — the unit
# objects include iouring.o/pool.o, so they must link the same libs as the main
# binary or resolution fails on real glibc.
ldlibs=$(sed -n 's/^LDLIBS_OPT += //p' config.mk | tr '\n' ' ')

fail=0
for t in tests/unit/*_test.c; do
	[ -e "$t" ] || continue
	name=$(basename "$t" .c)
	if ! $CC $CFLAGS -o "$work/$name" "$t" $libsrc $ldlibs 2>"$work/$name.log"; then
		echo "BUILD FAIL: $name"
		cat "$work/$name.log"
		fail=1
		continue
	fi
	if ! "$work/$name"; then
		fail=1
	fi
done

# SR-1.5: the Makefile uses an explicit SRC list (deterministic + bmake-proof).
# Guard against drift — a new src/*.c silently omitted from the build is exactly
# the footgun the explicit list traded for. Compare the list to the glob.
sed -n '/^SRC = /,/[^\\]$/p' Makefile | sed 's/^SRC = //; s/\\//g' | tr -s ' \t' '\n' | grep '\.c$' | sort >"$work/mk_src"
ls src/*.c src/sys/*.c src/render/*.c 2>/dev/null | sort >"$work/fs_src"
if ! diff -q "$work/mk_src" "$work/fs_src" >/dev/null 2>&1; then
	echo "SRCLIST: Makefile SRC list is out of sync with src/*.c:"
	diff "$work/mk_src" "$work/fs_src" | sed -n '1,20p'
	fail=1
else
	echo "SRCLIST: Makefile SRC matches src/ ($(wc -l <"$work/fs_src" | tr -d ' ') files)"
fi

# Discovery check (Sprint 01): traversal must find exactly what tree finds.
if [ -f tests/golden/walk.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/walk.sh; then
		fail=1
	fi
fi

# Colorization parity (Sprint 07a).
if [ -f tests/golden/color.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/color.sh; then
		fail=1
	fi
fi

# PTY colorization parity (auto-color on a real terminal; skips if no pty).
if [ -f tests/golden/pty.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/pty.sh; then
		fail=1
	fi
fi

# -o FILE content parity (SR-0.4): the written file matches tree's stdout.
if [ -f tests/golden/outfile.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/outfile.sh; then
		fail=1
	fi
fi

# -R 00Tree.html generation parity (SR02-0.1): the generated per-dir files match
# tree's, which the stdout-only golden matrix can't see.
if [ -f tests/golden/rerun.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/rerun.sh; then
		fail=1
	fi
fi

# Cross-dir recursive-symlink cycle detection (SR02-0.2): build_level sorts before
# the descent loop so -l cycle decisions match tree across inode layouts.
if [ -f tests/golden/cyclic.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/cyclic.sh; then
		fail=1
	fi
fi

# Per-parser-flag golden coverage lint (SR-1.7): every flag has >=1 case.
if [ -f tests/golden/flag_coverage.sh ]; then
	if ! sh tests/golden/flag_coverage.sh; then
		fail=1
	fi
fi

# Usage/help/version/bad-flag parity (SR-1.6): name-only divergence from tree.
if [ -f tests/golden/usage.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/usage.sh; then
		fail=1
	fi
fi

# Deep-tree fd robustness (Sprint 11c): soft fd limit raised to hard.
if [ -f tests/golden/fdlimit.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/fdlimit.sh; then
		fail=1
	fi
fi

# Parallel-stat determinism (Sprint 11b): --threads must not change output.
if [ -f tests/golden/threads.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/threads.sh; then
		fail=1
	fi
fi

# Intentional deviations (SR-1.3): aspen does the correct thing where tree has a
# bug; assert aspen's correct behaviour so it can't regress toward tree's bug.
if [ -f tests/golden/deviations.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/deviations.sh; then
		fail=1
	fi
fi

# JSON/XML structural invariants (SR-1.10): aspen's -J/-X well-formed on its own.
if [ -f tests/golden/structural.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/structural.sh; then
		fail=1
	fi
fi

# Differential fuzzer (SR-1.3): seeded random trees vs tree 2.3.2. Small N here
# (CI gate); a nightly job runs FUZZ_N in the thousands.
if [ -f tests/golden/fuzz.sh ] && [ -x ./aspen ]; then
	if ! FUZZ_N=${FUZZ_N:-60} sh tests/golden/fuzz.sh; then
		fail=1
	fi
fi

# Golden parity suite — the core 243-case matrix vs tree 2.3.2. Gate on -f, not
# -x: these scripts run via `sh` and are NOT executable in a fresh checkout, so
# an -x gate silently SKIPPED the whole golden matrix under `gmake test`/CI (the
# fuzzer and walk.sh still ran, masking it). Found via `make coverage`.
if [ -f tests/golden/run.sh ]; then
	if ! sh tests/golden/run.sh; then
		fail=1
	fi
fi

if [ "$fail" -eq 0 ]; then
	echo "TESTS: all passed"
else
	echo "TESTS: FAILURES"
	exit 1
fi
