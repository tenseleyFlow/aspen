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
echo "built ref tree $TAG -> $bin"
