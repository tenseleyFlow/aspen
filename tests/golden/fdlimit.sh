#!/bin/sh
# Deep-tree fd robustness (Sprint 11c). The walk holds one open dir fd per active
# depth (fd-relative openat — the speed lever), so a deep chain can exceed the
# soft open-file limit. aspen raises its soft RLIMIT_NOFILE to the hard limit
# lazily — on the first descent that hits EMFILE (SR-3.9), so shallow runs pay no
# rlimit syscalls — then retries and descends as deep as the system allows. This
# checks that a deep chain under a low SOFT limit still completes (rc 0) with
# identical output — where the un-bumped walk would have errored (rc 2) partway down.
set -u

work=tests/.work
ASP=./aspen
chain="$work/fdchain"
fail=0

[ -x "$ASP" ] || { echo "FDLIMIT: aspen not built"; exit 1; }

# Need headroom: skip cleanly if the hard limit itself is low (can't bump).
hard=$(ulimit -Hn 2>/dev/null || echo 0)
case "$hard" in
unlimited) hard=1000000 ;;
''|*[!0-9]*) hard=0 ;;
esac
if [ "$hard" -lt 4096 ]; then
	echo "FDLIMIT: hard limit $hard too low to test bump — skipping"
	exit 0
fi

# Deep chain (250 levels) — kept under PATH_MAX so it is well-formed everywhere.
if [ ! -d "$chain" ]; then
	p="$chain"
	mkdir -p "$p"
	i=0
	while [ "$i" -lt 250 ]; do
		p="$p/d"
		i=$((i + 1))
	done
	mkdir -p "$p"
fi

"$ASP" "$chain" >"$work/fd.ref" 2>/dev/null
refrc=$?

# Low soft limit (96) — fewer than the chain depth; the un-bumped walk fails.
( ulimit -Sn 96 2>/dev/null; "$ASP" "$chain" >"$work/fd.low" 2>/dev/null )
lowrc=$?

if [ "$lowrc" -ne 0 ]; then
	echo "FDLIMIT: aspen failed (rc=$lowrc) under low soft fd limit — rlimit bump not working"
	fail=1
elif ! diff -q "$work/fd.ref" "$work/fd.low" >/dev/null 2>&1; then
	echo "FDLIMIT: output differs under low soft fd limit"
	fail=1
fi

if [ "$fail" -eq 0 ]; then
	echo "FDLIMIT: deep chain survives a low soft fd limit (soft raised to hard)"
else
	exit 1
fi
