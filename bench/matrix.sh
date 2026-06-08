#!/bin/sh
# Comprehensive aspen-vs-tree performance matrix -> CSV (for the README tables).
#
# Honest by construction: every configuration benchmarked is one the golden suite
# proved byte-for-byte identical to tree, output is sent to /dev/null (compute,
# not terminal), and aspen + tree get the *same* flags. Both parity targets are
# measured: tree 2.3.2 (the spec) and 2.2.1.
#
# Usage:
#   sh bench/matrix.sh MACHINE_LABEL [PLAN]
#     PLAN = warm (default) | cold | all
#   env: REAL=1  also clone+bench a real repo (the Linux kernel, shallow)
#        HF=<path to hyperfine>   REPS=<n>  WARMUP=<n>
#        SUDO_DROP="sudo sh -c 'sync; echo 3 >/proc/sys/vm/drop_caches'"  (cold)
#
# Output: bench/.work/matrix-<MACHINE>.csv with columns:
#   machine,shape,entries,flags,locale,cache,tool,mean,min,stddev
set -u

MACH=${1:?usage: matrix.sh MACHINE_LABEL [warm|cold|all]}
PLAN=${2:-warm}
HF=${HF:-hyperfine}
REPS=${REPS:-20}
WARMUP=${WARMUP:-5}
REAL=${REAL:-0}
work=bench/.work
out="$work/matrix-$MACH.csv"
mkdir -p "$work"

command -v "$HF" >/dev/null 2>&1 || { echo "FATAL: hyperfine not found (set HF=)"; exit 1; }
ASP=./aspen
[ -x "$ASP" ] || { echo "building aspen release..."; make release >/dev/null 2>&1 || gmake release >/dev/null 2>&1 || { echo "build failed"; exit 1; }; }
refdir=tests/.work/ref
T232="$refdir/tree-2.3.2"; T221="$refdir/tree-2.2.1"
[ -x "$T232" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null 2>&1 || true
[ -x "$T221" ] || sh tests/golden/build-ref.sh 2.2.1 >/dev/null 2>&1 || true
[ -x "$T232" ] || { echo "FATAL: no tree 2.3.2"; exit 1; }
HAVE221=0; [ -x "$T221" ] && HAVE221=1

# --- corpus (built once, reused) ---
C=$work/corpus
gen() { # shape builder
	case $1 in
	flat) d=$C/flat; [ -d "$d" ] && return; mkdir -p "$d"
	      seq 1 200000 | awk -v p="$d" '{printf "%s/f%06d\n",p,$1}' | xargs -P4 -n3000 touch ;;
	wide) d=$C/wide; [ -d "$d" ] && return; i=0
	      while [ "$i" -lt 4000 ]; do s="$d/d$(printf %04d "$i")"; mkdir -p "$s"
	          seq 1 50 | awk -v p="$s" '{printf "%s/f%02d\n",p,$1}' | xargs -n50 touch; i=$((i+1)); done ;;
	deep) d=$C/deep; [ -d "$d" ] && return; mkdir -p "$d"; p="$d"; i=0
	      while [ "$i" -lt 400 ]; do p="$p/d"; mkdir -p "$p"; i=$((i+1)); done; : > "$p/leaf" ;;
	mixed) d=$C/mixed; [ -d "$d" ] && return; sh bench/mkcorpus.sh "$d" 100 50 40 >/dev/null ;;
	real) d=$C/real; [ -d "$d/.git" ] && return; rm -rf "$d"
	      git clone --depth 1 -q https://github.com/torvalds/linux "$d" 2>/dev/null || { echo "real clone failed"; return 1; } ;;
	esac
}
entries() { find "$1" 2>/dev/null | wc -l | tr -d ' '; }

# --- locales available ---
have_locale() { [ "$(LC_ALL=$1 locale charmap 2>/dev/null)" = "UTF-8" ] || [ "$1" = "C" ]; }

# --- run one cell: aspen vs tree(s), append parsed rows ---
# args: shape flags locale cache(warm|cold)
cell() {
	_shape=$1; _flags=$2; _lc=$3; _cache=$4
	_dir="$C/$_shape"; [ -d "$_dir" ] || return
	_ent=$(entries "$_dir")
	_csv="$work/_hf.csv"; _prep=""
	# HONESTY GATE: only benchmark configs whose output is byte-identical to tree
	# (normalize just the leading program-name token, which appears only in errors).
	LC_ALL=$_lc $ASP $_flags "$_dir" 2>&1 | sed 's/^aspen:/X:/' > "$work/_pa" 2>/dev/null
	LC_ALL=$_lc $T232 $_flags "$_dir" 2>&1 | sed 's/^tree:/X:/' > "$work/_pt" 2>/dev/null
	if ! cmp -s "$work/_pa" "$work/_pt"; then echo "  SKIP (not byte-identical): $_shape [$_flags] $_lc"; return; fi
	if [ "$_cache" = cold ]; then
		[ -n "${SUDO_DROP:-}" ] || { echo "cold: SUDO_DROP unset, skipping"; return; }
		_prep="--prepare"; _w=0
	else _w=$WARMUP; fi
	# build the command list (identical flags to each binary; sink to /dev/null)
	set -- "LC_ALL=$_lc $ASP $_flags '$_dir' >/dev/null 2>&1" \
	       "LC_ALL=$_lc $T232 $_flags '$_dir' >/dev/null 2>&1"
	[ "$HAVE221" = 1 ] && set -- "$@" "LC_ALL=$_lc $T221 $_flags '$_dir' >/dev/null 2>&1"
	# cold also shows the opt-in read-prefetch (warm would just double-read)
	[ "$_cache" = cold ] && set -- "$@" "ASP_PREFETCH=1 LC_ALL=$_lc $ASP $_flags '$_dir' >/dev/null 2>&1"
	if [ "$_cache" = cold ]; then
		"$HF" --shell sh -w 0 -r "${COLDREPS:-8}" --prepare "$SUDO_DROP" --export-csv "$_csv" "$@" >/dev/null 2>&1 || { echo "  hf failed: $_shape $_flags/$_lc/$_cache"; return; }
	else
		"$HF" --shell sh -w "$_w" -r "$REPS" --export-csv "$_csv" "$@" >/dev/null 2>&1 || { echo "  hf failed: $_shape $_flags/$_lc/$_cache"; return; }
	fi
	# parse: hyperfine csv = command,mean,stddev,median,user,system,min,max
	awk -F, -v m="$MACH" -v sh="$_shape" -v e="$_ent" -v fl="[$_flags]" -v lc="$_lc" -v ca="$_cache" '
	  NR>1 { cmd=$1; tool=(cmd ~ /ASP_PREFETCH/)?"aspen-prefetch":((cmd ~ /\/aspen|[ =]\.\/aspen/)?"aspen":(cmd ~ /2\.3\.2/?"tree-2.3.2":(cmd ~ /2\.2\.1/?"tree-2.2.1":"?")));
	         printf "%s,%s,%s,%s,%s,%s,%s,%.5f,%.5f,%.5f\n", m,sh,e,fl,lc,ca,tool,$2,$7,$3 }' "$_csv" >> "$out"
	# progress line with speedups vs 2.3.2
	awk -F, -v sh="$_shape" -v fl="$_flags" -v lc="$_lc" -v ca="$_cache" '
	  NR>1 { c=$1; v=(c~/aspen/)?"a":(c~/2\.3\.2/?"t2":(c~/2\.2\.1/?"t1":"?")); mean[v]=$2; mn[v]=$7 }
	  END { sp=(mean["a"]>0)?mean["t2"]/mean["a"]:0; printf "  %-6s %-10s %-4s %-4s aspen=%.4f tree232=%.4f -> %.2fx\n", sh,fl,lc,ca,mean["a"],mean["t2"],sp }' "$_csv"
}

echo "machine,shape,entries,flags,locale,cache,tool,mean,min,stddev" > "$out"
echo "== matrix on '$MACH' plan=$PLAN  aspen=$($ASP --version)  reps=$REPS =="

SHAPES="flat wide deep mixed"
[ "$REAL" = 1 ] && SHAPES="$SHAPES real"
for s in $SHAPES; do gen "$s" || true; done

if [ "$PLAN" = warm ] || [ "$PLAN" = all ]; then
	# Table 1: default across all shapes
	for s in $SHAPES; do cell "$s" "" C warm; done
	# Table 2: flag sweep on mixed + wide
	for fl in "-a" "-s" "-h" "-p" "-u" "-g" "-D" "-J" "-X" "-C" "--du" "--prune" "-L 2" "-t" "-v" "-U" "-f"; do
		cell mixed "$fl" C warm
	done
	# Table 5: locale sweep (sort/name-sensitive) on flat + mixed
	for lc in C C.UTF-8 en_US.UTF-8; do
		have_locale "$lc" || continue
		cell flat "" "$lc" warm; cell flat "-s" "$lc" warm; cell mixed "" "$lc" warm
	done
fi

if [ "$PLAN" = cold ] || [ "$PLAN" = all ]; then
	# Cold: default + -s across shapes (aspen serial vs tree; prefetch handled separately)
	for s in $SHAPES; do cell "$s" "" C cold; cell "$s" "-s" C cold; done
fi

echo "== wrote $out ($(($(wc -l < "$out")-1)) rows) =="
