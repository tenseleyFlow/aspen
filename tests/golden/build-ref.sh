#!/bin/sh
# Build the reference `tree` (parity target 2.3.2) into tests/.work/ref/.
# Uses the local clone in .docs/refs/unix-tree when present (dev box); otherwise
# clones upstream (CI / fresh checkouts, where .docs/ is gitignored & absent).
# Idempotent.
set -eu

TAG=${1:-2.3.2}
UPSTREAM=https://gitlab.com/OldManProgrammer/unix-tree.git
OUT=tests/.work/ref
bin="$OUT/tree-$TAG"

mkdir -p "$OUT"
[ -x "$bin" ] && { echo "ref tree $TAG present"; exit 0; }

if [ -d ".docs/refs/unix-tree/.git" ]; then
	REFREPO=.docs/refs/unix-tree
	( cd "$REFREPO" && git checkout -q "$TAG" )
else
	REFREPO=tests/.work/unix-tree
	if [ ! -d "$REFREPO/.git" ]; then
		git clone --depth 1 --branch "$TAG" "$UPSTREAM" "$REFREPO" 2>/dev/null \
			|| git clone "$UPSTREAM" "$REFREPO"
	fi
	( cd "$REFREPO" && git checkout -q "$TAG" 2>/dev/null || true )
fi

( cd "$REFREPO" \
	&& { gmake -s clean >/dev/null 2>&1 || make -s clean >/dev/null 2>&1 || true; } \
	&& { gmake -s >/dev/null 2>&1 || make -s >/dev/null 2>&1; } )
cp "$REFREPO/tree" "$bin"

# Guard the build: a shared/local clone (.docs/refs/unix-tree) can be left on the
# wrong tag — that is exactly how the audit A4 bug entered (the working tree sat at
# 2.2.1). Assert the binary actually reports the requested version before any test
# trusts it as the parity oracle.
got=$("$bin" --version 2>/dev/null | sed -n '1s/.*tree v\([0-9.]*\).*/\1/p')
if [ "$got" != "$TAG" ]; then
	echo "build-ref: $bin reports version '$got', expected '$TAG' — wrong checkout (refusing it)" >&2
	rm -f "$bin"
	exit 1
fi
echo "built ref tree $TAG -> $bin"
