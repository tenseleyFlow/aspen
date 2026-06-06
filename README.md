# aspen

`tree`, but blazingly fast.

aspen is a from-scratch C reimplementation of [`tree(1)`](https://gitlab.com/OldManProgrammer/unix-tree)
that aims to be **byte-for-byte identical** to stock `tree` while running **measurably faster** on
every workload. Binaries: `aspen` and `asp`.

## Status

Early development. The core thesis is validated: a proof-of-concept that reads directories with
`getdents`/`d_type` and **skips the per-entry `stat` that tree always pays** is byte-identical to
`tree` on a static tree and **~5× faster** (40k-file corpus, warm cache, single-threaded, no
arena/io_uring yet — the conservative floor). Build it out per the sprints below.

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
