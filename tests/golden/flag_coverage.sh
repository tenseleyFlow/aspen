#!/bin/sh
# Per-parser-flag golden coverage lint (SR-1.7). Every flag the option parser
# accepts must be exercised by at least one golden case — otherwise a flag can
# rot (or never have worked) with the suite still green. The inventory is
# DERIVED from src/options.c (so a newly-added flag with no test fails this
# lint, not just an out-of-date hand list), and checked against the CASES block
# in run.sh plus an allowlist of flags covered by dedicated scripts.
set -u
opt=src/options.c
run=tests/golden/run.sh
work=tests/.work
mkdir -p "$work"
fail=0

[ -f "$opt" ] || { echo "FLAGCOV: $opt missing"; exit 1; }

# --- inventory: short flags (the parser's switch(c)) -------------------------
grep -oE "case '[A-Za-z]'" "$opt" | grep -oE "[A-Za-z]" | sort -u >"$work/fc.short"

# --- inventory: long flags (strcmp + long_val literals) ----------------------
# Strip the leading --, drop the bare "--" terminator.
grep -oE '"--[a-z][a-z-]*"' "$opt" | tr -d '"' | sed 's/^--//' | sort -u >"$work/fc.long"

# --- covered: CASES block (between CASES='...' and its closing quote) --------
awk '
	/^CASES='"'"'/ { f=1; sub(/^CASES='"'"'/, ""); print; next }
	f && /'"'"'$/   { sub(/'"'"'$/, ""); print; exit }
	f               { print }
' "$run" >"$work/fc.cases"

# Short flags present: every letter inside any single-dash (non --) cluster.
grep -oE '(^| )-[A-Za-z]+' "$work/fc.cases" | tr -d ' -' | fold -w1 | sort -u >"$work/fc.short_cov"
# Long flags present: --word tokens, =value stripped.
grep -oE -- '--[a-z][a-z-]*' "$work/fc.cases" | sed 's/^--//' | sort -u >"$work/fc.long_cov"

# Flags exercised by dedicated harnesses rather than the CASES matrix.
#   -o        -> outfile.sh        --threads -> threads.sh (aspen-only)
#   help/version -> usage.sh
printf 'o\n'            >>"$work/fc.short_cov"
printf 'help\nversion\nthreads\n' >>"$work/fc.long_cov"
sort -u "$work/fc.short_cov" -o "$work/fc.short_cov"
sort -u "$work/fc.long_cov"  -o "$work/fc.long_cov"

miss_short=$(comm -23 "$work/fc.short" "$work/fc.short_cov" | tr '\n' ' ')
miss_long=$(comm -23 "$work/fc.long" "$work/fc.long_cov" | tr '\n' ' ')

[ -n "$(echo "$miss_short" | tr -d ' ')" ] && { echo "FLAGCOV: short flags with no golden case: $miss_short"; fail=1; }
[ -n "$(echo "$miss_long"  | tr -d ' ')" ] && { echo "FLAGCOV: long flags with no golden case: $miss_long"; fail=1; }

if [ "$fail" -eq 0 ]; then
	echo "FLAGCOV: all $(( $(wc -l <"$work/fc.short") + $(wc -l <"$work/fc.long") )) parser flags have >=1 golden case"
else
	exit 1
fi
