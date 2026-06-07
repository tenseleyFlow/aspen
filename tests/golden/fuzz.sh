#!/bin/sh
# Differential fuzzer (SR-1.3). Builds seeded pseudo-random directory trees —
# varied breadth/depth, dotfiles, weird/unicode/control-char names, symlinks
# (siblings, parent cycles, broken), unreadable dirs, empty dirs — runs aspen
# and tree 2.3.2 over a seed-derived flag sample in C and a UTF-8 locale, and
# diffs stdout+stderr+exit after normalizing the (sanctioned) program name. Any
# divergence prints the seed, flags, locale and diff so it is reproducible.
#
#   FUZZ_N=<iters>   how many trees (default 40; nightly CI uses thousands)
#   FUZZ_SEED=<n>    base seed (default 1) — FUZZ_SEED=K FUZZ_N=1 replays tree K+1
#
# Trees are generated with awk's srand()/rand(); the exact sequence can vary
# across awk implementations, so a replayed seed reproduces a failure only on
# the same platform/awk (printed below). That is fine for a fuzzer — different
# platforms simply explore different trees.
set -u
work=tests/.work
ref="$work/ref/tree-2.3.2"
ASP=./aspen
fuzz="$work/fuzz"
N=${FUZZ_N:-40}
base=${FUZZ_SEED:-1}

[ -x "$ASP" ] || { echo "FUZZ: aspen not built"; exit 1; }
[ -x "$ref" ] || sh tests/golden/build-ref.sh 2.3.2 >/dev/null || { echo "FUZZ: no ref"; exit 1; }

# UTF-8 locale if one is installed (skip gracefully on musl/Alpine).
utf8=""
for L in en_US.UTF-8 en_US.utf8 C.UTF-8 C.utf8; do
	[ "$(LC_ALL=$L locale charmap 2>/dev/null)" = "UTF-8" ] && { utf8=$L; break; }
done
locales="C"; [ -n "$utf8" ] && locales="C $utf8"

# Normalize the sanctioned program name, and neutralize DEVIATION D2: tree's -J,
# after any error, leaks an empty ,"contents":[    ] (or [] under -i) onto later
# entries; aspen omits it (valid JSON). An EMPTY contents array is always that
# quirk — tree emits no contents key for a genuinely empty dir, and an error dir's
# array is non-empty ([{"error":...}    ]) — so stripping it only cancels the bug.
# (Regressions where aspen wrongly emits it are caught by deviations.sh, not here.)
norm() { sed 's/^tree: /PROG: /; s/^aspen: /PROG: /; s/^usage: tree /usage: PROG /; s/^usage: aspen /usage: PROG /; s/,"contents":\[ *\]//g'; }

# Well-formed, parity-safe flags to fuzz freely — the fuzzer targets walk/render
# output parity, so flags must be VALID forms (malformed-CLI parser quirks are
# tracked separately in SR-2.13). Excluded on purpose:
#   -R (writes 00Tree.html into the tree), -o (writes a file / empties stdout),
#   -H/-T (HTML embeds aspen's own name+version), --help/--version (not a walk),
#   -P/-I (need a separate pattern arg; pattern matching is golden-covered),
#   --filelimit (root-as-over-limit-arg is the deferred SR-2.12 divergence, which
#     a random root with >N entries hits constantly; re-add once 2.12 lands — the
#     child filelimit path is already golden-covered and correct),
#   --inodes/--device (SR-2.14: -J/-X emit LINK ino/dev where tree emits the
#     TARGET's; random trees have symlinks, so re-add once 2.14 lands — unix-mode
#     ino/dev is correct and golden-covered),
#   -f (SR-2.15: aspen doesn't thread the full path into -J/-X name; re-add once
#     fixed — unix/html -f is correct and golden-covered).
# These are gated elsewhere (golden matrix, usage.sh, outfile.sh).
FLAGPOOL="-a -d -i -l -x -s -h -p -u -g -D -F -Q -N -q -C -n -A -S -t -c -v -U -r \
--du --prune --dirsfirst --filesfirst --noreport --matchdirs \
--ignore-case --metafirst -J -X -L1 -L2 -L3"

echo "FUZZ: $N trees x [$locales] (awk: $(awk --version 2>/dev/null | head -1 || echo unknown))"
echo "FUZZ: base seed $base"
fail=0; checked=0

i=0
while [ "$i" -lt "$N" ]; do
	i=$((i + 1))
	seed=$((base + i))
	tree="$fuzz/t"
	# (re)build the tree
	rm -rf "$fuzz"; mkdir -p "$fuzz"
	awk -v seed="$seed" -v root="$tree" '
	function rnd(n) { return int(rand() * n) }
	function name(  pool, n, k) {
		split("plain dot.ext .hidden ..dd UP MiX tilde~ dollarX quoteQ parenX " \
		      "starX questX brackX ampX semiX pipeX hashX atX pctX sp_ace " \
		      "café naïve 日本語 Omega emojiE", pool, " ")
		n = 0; for (k in pool) n++
		return pool[rnd(n) + 1]
	}
	BEGIN {
		srand(seed); OFS = "\t"
		print "d", root
		depth = 1 + rnd(4)
		q[0] = root; qn = 1; made = 0
		for (lvl = 0; lvl < depth && qn > 0 && made < 120; lvl++) {
			nq = 0
			for (i = 0; i < qn; i++) {
				d = q[i]; kids = rnd(5)
				for (c = 0; c <= kids && made < 120; c++) {
					made++; nm = name() "_" made; p = d "/" nm; r = rnd(10)
					if (r < 4)      { print "d", p; if (nq < 60) nextq[nq++] = p }
					else if (r < 7) { print "f", p; if (rnd(4) == 0) print "x", p }
					else if (r < 8) { print "f", d "/.dot" made }
					else if (r < 9) {
						s = rnd(3)
						if (s == 0)      print "l", p, d
						else if (s == 1) print "l", p, "nope" made
						else             print "l", p, nm "_self"
					}
					else { print "d", p; if (rnd(2) == 0) print "m", p }
				}
			}
			qn = nq; for (j = 0; j < nq; j++) q[j] = nextq[j]
		}
	}' > "$fuzz/spec"
	# Execute the spec (tab-delimited so spaced names survive).
	while IFS='	' read -r op a b; do
		case "$op" in
		d) mkdir -p "$a" 2>/dev/null ;;
		f) : > "$a" 2>/dev/null ;;
		x) chmod +x "$a" 2>/dev/null ;;
		l) ln -s "$b" "$a" 2>/dev/null ;;
		m) chmod 000 "$a" 2>/dev/null ;;
		esac
	done < "$fuzz/spec"
	# a couple of control-char names to exercise -q/-N/default printing
	: > "$tree/$(printf 'ctrlA\001end')" 2>/dev/null || :
	: > "$tree/$(printf 'del\177x')" 2>/dev/null || :

	# Seed-derived flag sample: 1-4 flags from the pool.
	flags=$(awk -v seed="$((seed * 7 + 3))" -v pool="$FLAGPOOL" '
	BEGIN {
		srand(seed); n = split(pool, a, /[ \t\n]+/); k = 1 + int(rand() * 4); s = ""
		for (j = 0; j < k; j++) s = s (j ? " " : "") a[1 + int(rand() * n)]
		print s
	}')

	for lc in $locales; do
		checked=$((checked + 1))
		LC_ALL="$lc" "$ASP" $flags "$tree" >"$fuzz/a.o" 2>"$fuzz/a.e"; ar=$?
		LC_ALL="$lc" "$ref" $flags "$tree" >"$fuzz/b.o" 2>"$fuzz/b.e"; br=$?
		norm <"$fuzz/a.o" >"$fuzz/a.on"; norm <"$fuzz/b.o" >"$fuzz/b.on"
		norm <"$fuzz/a.e" >"$fuzz/a.en"; norm <"$fuzz/b.e" >"$fuzz/b.en"
		out_ok=0
		diff -q "$fuzz/a.on" "$fuzz/b.on" >/dev/null 2>&1 &&
		diff -q "$fuzz/a.en" "$fuzz/b.en" >/dev/null 2>&1 && out_ok=1
		# DEVIATION D1 (.docs/deviations.md): under a full-tree mode tree wrongly
		# exits 0 on an open/filelimit error; aspen correctly exits 2. Output is
		# identical, only the rc deviates in that exact direction — accept it.
		rc_ok=0; [ "$ar" = "$br" ] && rc_ok=1
		case " $flags " in
		*\ --du\ *|*\ --prune\ *|*\ --matchdirs\ *)
			[ "$ar" = 2 ] && [ "$br" = 0 ] && rc_ok=1 ;;
		esac
		if [ "$out_ok" != 1 ] || [ "$rc_ok" != 1 ]; then
			fail=$((fail + 1))
			echo "FUZZ DIFF: seed=$seed locale=$lc rc(aspen=$ar tree=$br) flags=[$flags]"
			diff "$fuzz/b.on" "$fuzz/a.on" | sed -n '1,12p' | cat -v
			diff "$fuzz/b.en" "$fuzz/a.en" | sed -n '1,6p' | cat -v
			[ "$fail" -ge 10 ] && { echo "FUZZ: stopping after 10 diffs"; break; }
		fi
	done
	[ "$fail" -ge 10 ] && break
	# restore perms so cleanup can remove chmod-000 dirs
	chmod -R u+rwx "$fuzz" 2>/dev/null || :
done

chmod -R u+rwx "$fuzz" 2>/dev/null || :
rm -rf "$fuzz"

if [ "$fail" -eq 0 ]; then
	echo "FUZZ: $checked runs, 0 diffs"
else
	echo "FUZZ: $fail diffs (replay: FUZZ_SEED=<seed-1> FUZZ_N=1)"
	exit 1
fi
