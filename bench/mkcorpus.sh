#!/bin/sh
# Benchmark corpus: a mixed wide+deep tree. Scale args keep CI fast while staying
# well above timing noise. Deterministic layout.
set -eu

root=${1:?usage: mkcorpus.sh DIR [DIRS SUBS FILES]}
D=${2:-30}
S=${3:-25}
F=${4:-12}

rm -rf "$root"
mkdir -p "$root"
i=0
while [ "$i" -lt "$D" ]; do
	j=0
	while [ "$j" -lt "$S" ]; do
		dir="$root/d$(printf %03d "$i")/s$(printf %03d "$j")"
		mkdir -p "$dir"
		k=0
		while [ "$k" -lt "$F" ]; do
			: > "$dir/f$(printf %03d "$k").dat"
			k=$((k + 1))
		done
		j=$((j + 1))
	done
	i=$((i + 1))
done
echo "bench corpus: $(find "$root" -type f | wc -l) files, $(find "$root" -type d | wc -l) dirs"
