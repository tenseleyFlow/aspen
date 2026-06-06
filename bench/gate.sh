#!/bin/sh
# Performance gate: FAIL unless aspen is faster than tree.
# Honest rule (see .docs/audits): only gate configs the golden suite proved
# byte-identical. Parses hyperfine CSV (mean is column 2, seconds).
set -u

csv=${1:?usage: gate.sh results.csv}
margin=${ASP_PERF_MARGIN:-1.00} # require aspen_mean * margin <= tree_mean (1.00 = strictly faster)

a=$(awk -F, 'NR>1 && /aspen/ {print $2; exit}' "$csv")
t=$(awk -F, 'NR>1 && /tree/  {print $2; exit}' "$csv")
[ -n "$a" ] && [ -n "$t" ] || {
	echo "PERF GATE: could not parse means from $csv"
	exit 1
}

verdict=$(awk -v a="$a" -v t="$t" -v m="$margin" 'BEGIN { printf (a * m <= t) ? "PASS" : "FAIL" }')
speedup=$(awk -v a="$a" -v t="$t" 'BEGIN { if (a > 0) printf "%.2f", t / a; else printf "inf" }')
echo "PERF GATE: aspen=${a}s tree=${t}s (${speedup}x) -> $verdict"
[ "$verdict" = PASS ]
