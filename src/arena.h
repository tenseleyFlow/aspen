#ifndef ASP_ARENA_H
#define ASP_ARENA_H

#include <stddef.h>

/*
 * Region/bump allocator with stack (mark/rewind) discipline — the allocation
 * backbone for traversal. Entries and names are bump-allocated from large
 * slabs instead of per-entry malloc (see .docs/audits/02 §B3, 03 §4).
 *
 * DFS usage: a directory takes a mark, allocates its entries, recurses (the
 * child marks/allocates/rewinds), then on return the parent's entries persist.
 * Streaming mode rewinds per subtree; full-tree mode keeps everything.
 */

struct ablock;

struct arena {
	struct ablock *head; /* first slab */
	struct ablock *cur;  /* slab the bump cursor is in */
	size_t default_payload;
};

struct arena_marker {
	struct ablock *blk;
	size_t off;
};

void arena_init(struct arena *a, size_t default_payload); /* 0 => 64 KiB slabs */
void arena_destroy(struct arena *a);                      /* free all slabs */
void arena_reset(struct arena *a);                        /* rewind to start, keep slabs */

void *arena_alloc(struct arena *a, size_t size);                       /* max_align_t aligned */
void *arena_alloc_aligned(struct arena *a, size_t size, size_t align); /* align: power of two */
char *arena_strdup(struct arena *a, const char *s);
void *arena_memdup(struct arena *a, const void *p, size_t n);

struct arena_marker arena_mark(struct arena *a);
void arena_rewind(struct arena *a, struct arena_marker m);

#endif /* ASP_ARENA_H */
