#!/bin/sh
# Fixtures for --fromfile / --fromtabfile golden cases: text path lists and a
# tab-indented tree (NOT real directories). Deterministic content only.
set -eu
d=${1:?usage: mkff.sh DIR}
rm -rf "$d"
mkdir -p "$d"

# Newline-separated path list: shared prefixes, trailing-slash dir, deep chain,
# a leading comment, a dir name that matches a -P pattern (src), and a dotfile.
cat > "$d/paths.txt" <<'EOF'
# comment line, skipped
a/b/c.txt
a/b/d.log
a/e.c
f.txt
g/
h/i/j/deep.c
src/main.c
src/util.h
.hidden/secret.txt
EOF

# --fflinks path list with " -> " targets.
cat > "$d/links.txt" <<'EOF'
bin/sh -> /usr/bin/sh
lib/libc.so
doc/readme -> ../README
EOF

# Tab-indented tree, including a link line for --fflinks.
printf 'root\n\tsub1\n\t\tfile1.c\n\t\tfile2.txt\n\tsub2\n\t\tinner -> /tmp/x\n\torphan.txt\n' \
	> "$d/tabs.txt"

# Tab file with an orphaned (over-indented) line -> stderr warning, rc 0.
printf 'top\n\t\ttoodeep.txt\nnext\n' > "$d/tab_orphan.txt"
