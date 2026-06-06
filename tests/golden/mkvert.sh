#!/bin/sh
# Adversarial version/numeric names to exercise -v / --sort=version (the tree-doc
# cases plus mixed numeric/leading-zero names).
set -eu

root=${1:?usage: mkvert.sh DIR}
rm -rf "$root"
mkdir -p "$root"
for n in file1 file2 file10 file100 v1.9 v1.10 v1.5 alpha1 alpha001 alpha010 \
	 foo.0 foo.009 foo.09 item9 item10 1.0 1.0.1 2 10; do
	: > "$root/$n"
done

echo "vert fixture at $root"
