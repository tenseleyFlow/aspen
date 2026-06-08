#ifndef ASP_POOL_H
#define ASP_POOL_H

/*
 * A minimal persistent worker pool for data-parallel passes (Sprint 11b).
 *
 * The only thing aspen parallelizes is the per-entry metadata stat() — the
 * dominant cost of -s/-p/-D/--du on large directories (a serial fstatat per
 * file). Output order is decided by name before any stat runs, and concurrent
 * fstatat() on a shared dir fd is stateless, so this changes timing only, never
 * bytes. Workers touch disjoint entries; no locking on the hot path.
 *
 * asp_pool_for(p, n, fn, arg) calls fn(arg, i) for every i in [0, n) across the
 * pool's workers plus the calling thread, and returns once all have completed.
 * With p == NULL (or one worker) it just runs the loop inline — the serial path
 * pays nothing.
 */

#include <stddef.h>

struct asp_pool;

/* Create a pool of `workers` background threads (clamped to >= 0). Returns NULL
 * if workers <= 1 or threads can't be created — callers treat NULL as serial. */
struct asp_pool *asp_pool_create(int workers);
void asp_pool_destroy(struct asp_pool *p);

/* Run fn(arg, i) for i in [0, n). Blocks until all iterations finish. */
void asp_pool_for(struct asp_pool *p, size_t n, void (*fn)(void *, size_t), void *arg);

/* Default worker count when --threads is auto (0): online CPUs, capped. */
int asp_pool_default_workers(void);

#endif /* ASP_POOL_H */
