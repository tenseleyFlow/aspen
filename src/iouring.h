#ifndef ASP_IOURING_H
#define ASP_IOURING_H

/*
 * Optional io_uring backend for the metadata stat pass (Sprint 11c, Linux).
 *
 * An alternative to the pthread pool: batch the per-entry statx() through an
 * io_uring submission queue instead of N synchronous fstatat() calls. Fills the
 * same entry fields as the serial/pool path (asp_stat_at), so output is
 * byte-identical. Compile-time gated on ASP_HAS_LIBURING; when absent every
 * call is a no-op/NULL and the caller falls back to the pool or serial loop.
 */

#include "entry.h"

#include <stddef.h>

struct asp_ring;

/* Create a ring of the given queue depth. Returns NULL if io_uring is
 * unavailable at build time, disabled at runtime, or setup fails — callers
 * treat NULL as "use the pool/serial path". */
struct asp_ring *asp_ring_create(unsigned depth);
void asp_ring_destroy(struct asp_ring *r);

/* statx each of the n entries relative to dirfd (AT_SYMLINK_NOFOLLOW, matching
 * asp_stat_at follow=0): fill mode/ino/dev/exec and, when want_st, the full
 * e->st (which the caller pre-allocated). Per-entry failures set ENT_STAT_FAILED
 * for the caller to drop in order. Returns 0 on success, -1 if the batch could
 * not be driven at all (caller falls back). */
int asp_ring_stat_batch(struct asp_ring *r, int dirfd, struct entry **ents,
			size_t n, int want_st);

#endif /* ASP_IOURING_H */
