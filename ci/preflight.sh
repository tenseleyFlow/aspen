#!/bin/sh
# Local pre-flight for bigger pushes: build + test + bench on the Linux (hasu)
# and macOS arm64 (nomad) boxes over the Tailscale network, so portability and
# perf are verified on real hardware before pushing. The FreeBSD dev box is local.
#
# Each host clones the reference tree itself (see build-ref.sh), so we don't sync
# the gitignored .docs/ refs. Requires ssh access as mfwolffe@<host>.
set -u

HOSTS=${1:-"hasu nomad"}
REMOTE_DIR='~/.aspen-preflight'
rc=0

for host in $HOSTS; do
	echo "== preflight on $host =="
	if ! ssh "mfwolffe@$host" "rm -rf $REMOTE_DIR && mkdir -p $REMOTE_DIR" 2>/dev/null; then
		echo "$host: ssh failed — SKIP"
		rc=1
		continue
	fi
	rsync -az --delete \
		--exclude '.git' --exclude '.docs' \
		--exclude 'tests/.work' --exclude 'bench/.work' \
		--exclude '*.o' --exclude '*.d' --exclude '/aspen' --exclude '/asp' \
		--exclude 'config.mk' --exclude 'config.h' \
		./ "mfwolffe@$host:$REMOTE_DIR/" || { echo "$host: rsync failed"; rc=1; continue; }

	if ssh "mfwolffe@$host" "cd $REMOTE_DIR && ./configure \
		&& (gmake CFLAGS='-O2 -Werror' || make CFLAGS='-O2 -Werror') \
		&& (gmake test || make test) \
		&& (gmake bench || make bench)"; then
		echo "$host: OK"
	else
		echo "$host: FAIL"
		rc=1
	fi
done

[ "$rc" -eq 0 ] && echo "preflight: all hosts OK" || echo "preflight: failures (see above)"
exit $rc
