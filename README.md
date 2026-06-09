# aspen

A reimplementation of [`tree(1)`](https://gitlab.com/OldManProgrammer/unix-tree) in C. Output matches
GNU tree 2.3.2 byte for byte; it runs faster on the workloads measured. Two binaries, `aspen` and `asp`.

## Status

Covers tree 2.3.2's flag surface. A golden suite checks output byte for byte against a locally built
tree 2.3.2 (hundreds of cases across C, C.UTF-8, and a UTF-8 locale), backed by a differential fuzzer,
in CI on Ubuntu, macOS, FreeBSD, and musl/Alpine, plus an io_uring job. Where tree's output is
deterministic but wrong, aspen does the correct thing and records the difference in
`.docs/deviations.md`.

## Build

```sh
./configure        # probes the toolchain, writes config.h / config.mk
make               # builds ./aspen and ./asp   (gmake on *BSD)
make release       # -O3 -flto portable build
make debug         # ASan/UBSan build
make install       # honors PREFIX / DESTDIR
```

Needs a C11 compiler and a POSIX make (GNU make on Linux/macOS, `gmake` on FreeBSD). No third-party
dependencies; `liburing` is used if present.

## Test and benchmark

```sh
make test          # unit tests (ASan/UBSan) + golden parity vs a locally built tree 2.3.2
make bench         # hyperfine aspen vs tree; the gate fails if aspen isn't faster
sh ci/preflight.sh # build, test, bench on the Linux/macOS boxes over Tailscale
```

The golden suite builds the reference tree 2.3.2 and compares stdout, stderr, and exit code. The only
normalized difference is the leading program-name token on stderr.

## Performance

The main saving is not calling `lstat` when `d_type` already gives the type; the rest is `getdents`,
arena allocation with inline names, batched `write(2)`, and an MSD byte-radix name sort. On the corpora
measured aspen lists directories about 3–5× faster than tree, more when tree's per-file `lstat`
dominates, and about 1.3× when a flag (`-s`, `-p`, `-D`, `-C`, `--du`) forces a stat of every entry.

Method: `bench/matrix.sh` runs `hyperfine` (warm: 5 warmups, 15–20 runs; cold: `drop_caches` before
each run). Output goes to `/dev/null` to time compute, both tools get the same flags, and only
configurations the golden suite proves identical are timed. Numbers are against tree 2.3.2; 2.2.1 is
within a few percent. The corpora are deterministic and rebuildable.

Warm cache, default invocation, vs tree 2.3.2:

| Workload (≈200k entries) | Linux x86-64 (NVMe)¹ | macOS arm64 |
|---|---:|---:|
| flat: 200k files in one directory | 4.7× | 2.8× |
| wide: 4000 dirs × 50 files | 5.2× | 3.4× |
| mixed: nested dirs and files | 4.7× | 2.7× |
| deep: a 400-level chain | 2.5× | 1.6× |
| real-world: PostgreSQL source, ~8k files² | 1.9× | — |

The ratio grows with tree size. On an empty or tiny tree, or `--version`, aspen ties tree (about
1.0–1.1×): it makes slightly fewer syscalls, but process startup dominates and there is little to walk.
The 1.9× row is an ~8k-file project; large trees reach ~5×.

Speedup depends on whether a flag forces `stat`:

| Config (mixed corpus, Linux) | Speedup | Why |
|---|---:|---|
| default, `-a`, `-J`, `-X`, `-U`, `-f`, `--prune` | 4.2–5.2× | walk/sort/output bound; aspen skips the per-entry `lstat` |
| `-s` `-h` `-p` `-u` `-g` `-t` `--du` | 1.3–1.5× | both tools stat every entry |
| `-C` (color), `-D` (date) | 1.4–2.7× | needs the mode, so partly stat-bound |

Cold cache (Linux, `drop_caches` per run):

| Workload | default | `-s` |
|---|---:|---:|
| flat | 3.3× | 2.1× |
| mixed | 1.7× | 1.2× |
| wide | 2.0× | 1.2× |
| deep | 1.4× | 1.5× |

Cold, both tools are I/O-bound and the gap narrows. aspen is still ahead on each shape: fewer syscalls,
and on wide directories the pool overlaps the cold `stat` latency. The closest cases are `-s` on
stat-bound shapes (`wide`, `mixed`), about 1.2×.

¹ A FreeBSD box running these Linux binaries through a compat layer shows 8–10×; the layer taxes tree's
syscalls extra, so it is not used for the headline. ² The real-world number is a small tree, so it is a
lower bound.

## Layout

```
src/        implementation (sys/ is the only platform-aware layer)
tests/      unit/ harness + golden/ parity suite
bench/      corpus generator, hyperfine runner, perf gate
ci/         preflight script  (.github/workflows/ci.yml drives CI)
.docs/      design, audits, sprints  (local)
```

## Invariants

1. Parity is the floor: each release is a drop-in for tree 2.3.2.
2. Faster than tree, always; enforced by the CI perf gate.
3. Among parity-preserving choices, take the fastest.

## aspen extensions

These knobs do not exist in tree and do not change the output bytes; they only affect how the work is
scheduled. They are kept out of `--help` so `aspen --help` still matches `tree --help`, and tree
rejects them as unknown flags.

- `--threads N` — worker count for the parallel `stat` pass. `0` (default) sizes to the CPU count, `1`
  forces the serial path, `N` uses N workers. Output is identical for any `N`; only `stat()` is
  parallelized. `N` is parsed strictly (`0..65535`); junk or a negative value is an error, not a silent
  `0`.
- `ASP_IO=serial|uring` — stat backend. `serial` forces the inline path; `uring` uses Linux `io_uring`
  `statx` batches when available, else prints a note on stderr and falls back to the pool. The default
  is the pool when a stat-heavy flag is set.
- `ASP_PREFETCH=1` (experimental) — fans out `opendir`+`getdents` on each level's subdirectories to
  warm the cache ahead of the serial walk, to hide latency on slow storage (HDD/NFS). Under hyperfine
  with `drop_caches` it showed no benefit on SSD/NVMe (a cold walk there is bandwidth-bound, not
  latency-bound) and it makes warm runs slightly slower, so it is off by default. Output is identical
  either way; streaming path only.
