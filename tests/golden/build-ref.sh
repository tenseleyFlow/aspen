#!/bin/sh
# Build the reference `tree` (parity target 2.3.2) from the cloned source in
# .docs/refs/unix-tree, into tests/.work/ref/. Idempotent.
set -eu

TAG=${1:-2.3.2}
REFREPO=.docs/refs/unix-tree
OUT=tests/.work/ref
bin="$OUT/tree-$TAG"

mkdir -p "$OUT"
[ -x "$bin" ] && { echo "ref tree $TAG present"; exit 0; }
[ -d "$REFREPO/.git" ] || { echo "missing $REFREPO (clone unix-tree there)"; exit 1; }

( cd "$REFREPO" \
    && git checkout -q "$TAG" \
    && { gmake -s clean >/dev/null 2>&1 || make -s clean >/dev/null 2>&1 || true; } \
    && { gmake -s >/dev/null 2>&1 || make -s >/dev/null 2>&1; } )
cp "$REFREPO/tree" "$bin"
echo "built ref tree $TAG -> $bin"
