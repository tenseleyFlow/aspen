#!/bin/sh
# Performance gate: FAIL unless aspen is faster than tree on a parity-certified
# config. Honest rule (.docs/audits, deviations.md): only configs the golden
# suite proved byte-identical are ever benchmarked.
#
#   gate.sh results.csv [metric] [label]
#     metric = mean (default) | min   — min is the noise-robust estimator for
#              sub-5ms workloads (best observed run ~ true compute cost).
#
# hyperfine CSV columns: command,mean,stddev,median,user,system,min,max
set -u

csv=${1:?usage: gate.sh results.csv [mean|min] [label]}
metric=${2:-mean}
label=${3:-}
case "$metric" in
	min)  col=7 ;;
	*)    col=2 ; metric=mean ;;
esac
margin=${ASP_PERF_MARGIN:-1.00} # require aspen*margin <= tree (1.00 = strictly faster)

# Anchor on the BINARY (first token of the command column), not a substring of
# the whole line — the corpus path itself contains "aspen", which would match
# both rows. aspen's binary token ends in "aspen"; tree's contains "tree".
a=$(awk -F, -v c="$col" 'NR>1 { split($1,w," "); if (w[1] ~ /aspen$/ || w[1] == "./aspen") { print $c; exit } }' "$csv")
t=$(awk -F, -v c="$col" 'NR>1 { split($1,w," "); if (w[1] ~ /tree/) { print $c; exit } }' "$csv")
[ -n "$a" ] && [ -n "$t" ] || {
	echo "PERF GATE${label:+ [$label]}: could not parse $metric from $csv"
	exit 1
}

verdict=$(awk -v a="$a" -v t="$t" -v m="$margin" 'BEGIN { printf (a * m <= t) ? "PASS" : "FAIL" }')
speedup=$(awk -v a="$a" -v t="$t" 'BEGIN { if (a > 0) printf "%.2f", t / a; else printf "inf" }')
printf 'PERF GATE%s: aspen=%ss tree=%ss (%sx, %s) -> %s\n' \
	"${label:+ [$label]}" "$a" "$t" "$speedup" "$metric" "$verdict"
[ "$verdict" = PASS ]
