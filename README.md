# aspen

`tree`, but blazingly fast.

aspen is a from-scratch C reimplementation of [`tree(1)`](https://gitlab.com/OldManProgrammer/unix-tree)
that aims to be **byte-for-byte identical** to stock `tree` while running **measurably faster** on
every workload. Binaries: `aspen` and `asp`.

## Status

Feature-complete against tree 2.3.2's flag surface and byte-for-byte identical, enforced by a
golden parity suite (hundreds of cases × C / C.UTF-8 / a dictionary UTF-8 locale) plus a
differential fuzzer, run in CI on Ubuntu, macOS, FreeBSD, and musl/Alpine (and an io_uring job).
The speed thesis held and compounded: skipping the per-entry `stat` tree always pays (`getdents` +
`d_type`), arena allocation with inline names, batched `write`, an MSD byte-radix name sort, and an
opt-in stat-parallel backend (thread pool / io_uring) put it around **6–11× faster** than tree on
the standard corpora — enforced by a CI perf gate that fails any build slower than tree. Where tree
has a genuine bug, aspen does the correct thing instead and documents the deviation.

## Build

```sh
./configure        # probes the toolchain, writes config.h / config.mk
make               # builds ./aspen and ./asp   (use gmake on *BSD)
make release       # -O3 -flto portable build
make debug         # ASan/UBSan build
make install       # honors PREFIX / DESTDIR
```

Requires a C11 compiler and a POSIX make (GNU make on Linux/macOS, `gmake` on FreeBSD). No
third-party dependencies; `liburing` is used only if present and is optional.

## Test & benchmark

```sh
make test          # unit tests (ASan/UBSan) + golden parity vs a locally-built tree 2.3.2
make bench         # hyperfine aspen vs tree; the perf gate fails if aspen isn't faster
sh ci/preflight.sh # build+test+bench on the Linux/macOS boxes over Tailscale
```

The golden suite builds the reference `tree` 2.3.2 itself; parity is byte-exact on stdout, stderr,
and exit code (the only normalized difference is the leading program-name token on stderr).

## Layout

```
src/        implementation (sys/ is the only platform-aware layer)
tests/      unit/ harness + golden/ parity suite
bench/      corpus generator, hyperfine runner, perf gate
ci/         preflight script  (.github/workflows/ci.yml drives CI)
.docs/      design + audits + sprints  (local; see the maintainer notes)
```

## Invariants

1. **Parity is the floor** — every release is a drop-in for `tree` 2.3.2.
2. **Faster than tree, always** — enforced by the CI perf gate.
3. Among parity-preserving choices, the **fastest** one wins.

## aspen extensions

These are aspen-only knobs that **do not exist in `tree`** and **never change the output bytes** —
they only tune *how* the work is done, so default behavior stays byte-identical. They are kept out
of `--help` deliberately (so `aspen --help` still matches `tree --help` exactly); `tree` rejects
them as unknown flags.

- **`--threads N`** — worker count for the parallel metadata-`stat` pass. `0` (default) auto-sizes to
  the CPU count; `1` forces the serial path (no pool); `N` uses N workers. Output is identical
  regardless — only `stat()` is parallelized.
- **`ASP_IO=serial|uring`** (env) — pick the stat backend: `serial` forces the inline path; `uring`
  uses Linux `io_uring` `statx` batches when available (else it says so on stderr and falls back to
  the thread pool). Default is the thread pool when a stat-heavy flag is in play.
- **`ASP_PREFETCH=1`** (env) — cross-directory read-prefetch: fan out `opendir`+`getdents` on each
  level's subdirectories (via the thread pool) to warm the kernel cache ahead of the serial walk.
  A **cold-cache** win only — measured ~2.3× faster on a cold 6000-dir tree (and more on
  HDD/NFS) — so it is off by default: when the cache is already warm it just costs an extra read.
  Output is byte-identical either way. Currently applies to the streaming (non-`-J/-X/--du/--prune`)
  path.
