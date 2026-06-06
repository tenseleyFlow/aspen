#!/bin/sh
# aspen test driver. Runs unit tests now; the golden parity suite is appended
# by a later Sprint-00 chunk (tests/golden/). Each unit test links against all
# library sources (everything in src/ except main.c) so cross-module deps resolve.
set -u

CC=${CC:-cc}
CFLAGS="-std=c11 -O1 -g -Isrc -I. -Itests/unit -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all"
work=tests/.work
mkdir -p "$work"

[ -f config.h ] || ./configure >/dev/null
libsrc=$(ls src/*.c src/sys/*.c src/render/*.c 2>/dev/null | grep -v '/main\.c$')

fail=0
for t in tests/unit/*_test.c; do
	[ -e "$t" ] || continue
	name=$(basename "$t" .c)
	if ! $CC $CFLAGS -o "$work/$name" "$t" $libsrc 2>"$work/$name.log"; then
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
