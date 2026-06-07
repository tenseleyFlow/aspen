#!/bin/sh
# JSON/XML structural invariants (SR-1.10). The golden matrix proves aspen's -J/-X
# is byte-identical to tree; this checks aspen's structured output is well-formed
# on ITS OWN terms (tree-independent) over an error-bearing, branching, deep tree
# — so a structural break is caught even on inputs outside the golden corpus.
set -u
work=tests/.work
ASP=./aspen
st="$work/struct"
fail=0

[ -x "$ASP" ] || { echo "STRUCT: aspen not built"; exit 1; }

# A tree with: a branch, depth, a nested unreadable dir (error node), and an
# empty dir (no contents key). Permission errors need a non-root euid; if root,
# we still validate structure (just without the error node).
rm -rf "$st"; mkdir -p "$st/a/b/c" "$st/d" "$st/empty"
: > "$st/a/f1"; : > "$st/a/b/f2"; : > "$st/d/f3"
mkdir -p "$st/a/noread"; chmod 000 "$st/a/noread"

# JSON well-formedness: brackets balance to zero and never go negative; exactly
# one report object; the report is introduced by tree's standalone leading comma.
chk_json() { # <flags...>
	"$ASP" -J "$@" "$st" >"$st/j" 2>/dev/null
	awk '
		{ line[NR] = $0 }
		{ for (i = 1; i <= length($0); i++) {
			ch = substr($0, i, 1)
			if (ch == "[" || ch == "{") depth++
			else if (ch == "]" || ch == "}") { depth--; if (depth < 0) bad = 1 }
		} }
		/"type":"report"/ { reports++ }
		END {
			if (bad)            { print "DEPTH<0"; exit 1 }
			if (depth != 0)     { print "UNBALANCED " depth; exit 1 }
			if (reports != 1)   { print "REPORTS " reports; exit 1 }
		}
	' "$st/j" >"$st/jmsg" 2>&1 || { echo "STRUCT json [$*]: $(cat "$st/jmsg")"; fail=1; return; }
	# first line "[", last line "]"
	head -1 "$st/j" | grep -qx '\[' || { echo "STRUCT json [$*]: no opening ["; fail=1; }
	tail -1 "$st/j" | grep -qx '\]' || { echo "STRUCT json [$*]: no closing ]"; fail=1; }
	# tree's report is preceded by a standalone leading comma on its line
	grep -q '^,.*"type":"report"' "$st/j" || { echo "STRUCT json [$*]: missing standalone-comma report line"; fail=1; }
}

# XML: exactly one <tree>/<report>, and every opened tag is closed (count <foo>
# vs </foo> per tag name; self-closed <foo .../> excluded).
chk_xml() { # <flags...>
	"$ASP" -X "$@" "$st" >"$st/x" 2>/dev/null
	head -1 "$st/x" | grep -q '<?xml ' || { echo "STRUCT xml [$*]: no xml declaration"; fail=1; }
	for tag in tree report; do
		_o=$(grep -c "<$tag>" "$st/x"); _c=$(grep -c "</$tag>" "$st/x")
		[ "$_o" = 1 ] && [ "$_c" = 1 ] || { echo "STRUCT xml [$*]: <$tag> open=$_o close=$_c (want 1/1)"; fail=1; }
	done
	# directory tags balance (open vs close); <file .../> are self-closed, ignore
	_do=$(grep -c '<directory ' "$st/x")
	_dc=$(grep -c '</directory>' "$st/x")
	[ "$_do" = "$_dc" ] || { echo "STRUCT xml [$*]: <directory> open=$_do close=$_dc"; fail=1; }
}

chk_json
chk_json -a
chk_json --du
chk_json -L 2
chk_xml
chk_xml -a
chk_xml --du
chk_xml -L 2

chmod -R u+rwx "$st" 2>/dev/null || :
rm -rf "$st"

if [ "$fail" -eq 0 ]; then
	echo "STRUCT: -J/-X well-formed (balanced, single report, standalone comma)"
else
	exit 1
fi
