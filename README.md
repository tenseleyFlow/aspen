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
`d_type`), arena allocation with inline names, batched `write`, and an MSD byte-radix name sort make
it **~3–5× faster** than tree at directory listing on typical hardware (see
[Performance](#performance) for the full warm/cold matrix) — enforced by a CI perf gate that fails
any build slower than tree. Where tree has a genuine bug, aspen does the correct thing instead and
documents the deviation.

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

## Performance

**aspen lists directories ~3–5× faster than `tree` on typical hardware**, widening on workloads
where `tree`'s per-file `lstat` dominates and narrowing to ~1.3× when *every* entry must be `stat`-ed
anyway (`-s`, `-p`, `-D`, color, `--du`). aspen's core lever is **not calling `lstat` when `d_type`
already answers the question**, plus arena allocation, batched `write(2)`, and an MSD byte-radix sort.

*Methodology:* [`bench/matrix.sh`](bench/matrix.sh) via `hyperfine` (warm: 5 warmups, 15–20 runs;
cold: `drop_caches` before every run). Output is discarded (`>/dev/null`) to measure compute, both
tools get identical flags, and **only configurations the golden suite proves byte-identical are ever
timed**. Compared against `tree` **2.3.2** *and* **2.2.1** (numbers below are vs 2.3.2; 2.2.1 is
within a few percent). All synthetic corpora are deterministic and rebuildable.

### Warm cache (the common case) — speedup vs `tree` 2.3.2, default invocation

| Workload (≈200k entries) | Linux x86-64 (NVMe)¹ | macOS arm64 |
|---|---:|---:|
| **flat** — 200k files in one directory | **4.7×** | **2.8×** |
| **wide** — 4 000 dirs × 50 files | **5.2×** | **3.4×** |
| **mixed** — nested dirs + files (typical) | **4.7×** | **2.7×** |
| **deep** — a 400-level chain (syscall-bound) | 2.5× | 1.6× |
| **real-world** — PostgreSQL source (~8k files)² | 1.9× | — |

The advantage **scales with tree size**. At the genuine process-startup floor — an empty or
tiny tree, or `--version` — aspen is a **tie** with `tree` (~1.0–1.1×, within noise): it issues
marginally fewer syscalls, but process startup dominates and there's almost nothing to walk. The
~1.9× above is already an ~8k-file project (startup is amortized there), and large trees reach ~5×.

### The honest nuance — speedup depends on whether a flag forces `stat`

| Config (mixed corpus, Linux) | Speedup | Why |
|---|---:|---|
| default, `-a`, `-J`, `-X`, `-U`, `-f`, `--prune` | **4.2–5.2×** | walk / sort / output bound — aspen skips the per-entry `lstat` |
| `-s` `-h` `-p` `-u` `-g` `-t` `--du` (metadata) | **1.3–1.5×** | both tools must `stat` every entry → `stat` I/O dominates |
| `-C` (color), `-D` (date) | 1.4–2.7× | needs the mode, so partly stat-bound |

### Cold cache — speedup vs `tree` 2.3.2 (Linux, `drop_caches` per run)

| Workload | default | `-s` |
|---|---:|---:|
| flat | **3.4×** | 2.0× |
| mixed | 1.5× | 1.1× |
| wide | 1.3× | 1.2× |
| deep | 1.2× | 0.9×³ |

Cold, both tools are I/O-bound, so the gap narrows; aspen still wins on the flat/output-heavy cases
by avoiding the per-file `lstat`.

¹ A FreeBSD box running these Linux binaries through a compat layer shows 8–10× — the layer taxes
`tree`'s heavy syscalls extra, so it's an outlier and not used for the headline. ² Real-world numbers
are small-tree (startup-floor) lower bounds. ³ `deep -s` is the one case where aspen is marginally
*slower* cold (the stat-pool hand-off costs more than it saves on a single-wide chain) — reported for
honesty.

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
  regardless — only `stat()` is parallelized. `N` is parsed strictly (`0..65535`); junk or a
  negative value is rejected with an error, not silently treated as `0`.
- **`ASP_IO=serial|uring`** (env) — pick the stat backend: `serial` forces the inline path; `uring`
  uses Linux `io_uring` `statx` batches when available (else it says so on stderr and falls back to
  the thread pool). Default is the thread pool when a stat-heavy flag is in play.
- **`ASP_PREFETCH=1`** (env, **experimental**) — cross-directory read-prefetch: fan out
  `opendir`+`getdents` on each level's subdirectories (via the thread pool) to warm the kernel cache
  ahead of the serial walk. The intent is to hide per-op latency on high-latency storage (HDD/NFS).
  **In rigorous benchmarking (hyperfine + `drop_caches` per run) it showed no measurable benefit on
  SSD/NVMe** — a cold walk there is bandwidth-bound, not latency-bound — and it makes warm runs
  slightly *slower* (an extra read pass). So it is **off by default** and kept only as an opt-in knob
  for high-latency storage we couldn't measure. Output is byte-identical either way; streaming path
  only.
