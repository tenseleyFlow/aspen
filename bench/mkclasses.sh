#!/bin/sh
# Generate the perf corpus *classes* the profiling pass exercises, at sizes that
# stay well above timing noise yet build fast enough for CI. Deterministic.
#   flat — one directory, many files     (stresses sort + output; tree lstats all)
#   wide — many dirs, moderate fan-out    (the common large-tree shape)
#   deep — a long single-entry chain      (syscall-bound; inherently serial)
set -eu
root=${1:?usage: mkclasses.sh DIR}
FLAT=${2:-20000}
WDIRS=${3:-200}
WFILES=${4:-100}
# Cap depth so the deepest FULL path stays under PATH_MAX (1024 on *BSD/macOS).
# Beyond that, tree's lstat on the long path fails ENAMETOOLONG and it silently
# drops the entry (its lstat-failure-drop behaviour) — diverging from aspen's
# fd-relative openat, which has no path-length limit. Keeping the chain short
# means tree and aspen emit IDENTICAL output, so the perf comparison is valid
# (a speed number is only honest against byte-identical output).
DEPTH=${5:-400}

mk_flat() {
	d="$root/flat"; [ -d "$d" ] && return 0
	mkdir -p "$d"
	jot "$FLAT" | awk -v p="$d" '{printf "%s/f%06d\n", p, $1}' | xargs -n 2000 touch
}
mk_wide() {
	d="$root/wide"; [ -d "$d" ] && return 0
	mkdir -p "$d"; j=0
	while [ "$j" -lt "$WDIRS" ]; do
		s="$d/d$(printf %03d "$j")"; mkdir -p "$s"
		jot "$WFILES" | awk -v p="$s" '{printf "%s/f%04d\n", p, $1}' | xargs -n 1000 touch
		j=$((j + 1))
	done
}
mk_deep() {
	d="$root/deep"; [ -d "$d" ] && return 0
	p="$d"; mkdir -p "$p"; i=0
	while [ "$i" -lt "$DEPTH" ]; do p="$p/d"; i=$((i + 1)); done
	mkdir -p "$p"
}

mk_flat; mk_wide; mk_deep
echo "classes: flat=$(ls -U "$root/flat" | wc -l)f  wide=$(find "$root/wide" -type f | wc -l)f/$(find "$root/wide" -type d | wc -l)d  deep=$(find "$root/deep" -type d | wc -l)d"
