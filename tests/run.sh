#!/bin/sh
# aspen test driver. Runs unit tests now; the golden parity suite is appended
# by a later Sprint-00 chunk (tests/golden/). Each unit test links against all
# library sources (everything in src/ except main.c) so cross-module deps resolve.
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

# Discovery check (Sprint 01): traversal must find exactly what tree finds.
if [ -x tests/golden/walk.sh ] && [ -x ./aspen ]; then
	if ! sh tests/golden/walk.sh; then
		fail=1
	fi
fi

# Colorization parity (Sprint 07a).
if [ -x tests/golden/color.sh ] && [ -x ./aspen ]; then
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

# Golden parity suite (present once tests/golden/run.sh lands).
if [ -x tests/golden/run.sh ]; then
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
