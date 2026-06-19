#ifndef ASP_TRAVERSE_H
#define ASP_TRAVERSE_H

/*
 * Depth-first traversal engine (serial). fd-relative, arena-backed entries, stat
 * only when a flag forces it. Emits each entry to a renderer in pre-order; the
 * renderer draws indentation from depth + is_last. Descent decisions (-L/-d/-x/-l)
 * live here.
 */

#include "entry.h"
#include "options.h"

struct totals {
	unsigned long dirs;
	unsigned long files;
	off_t size; /* --du: accumulated byte total */
};

struct renderer; /* render.h */

/* Metadata-stat backend (serial/pool/io_uring), built once and shared across all
 * roots (SR-2.4/2.5). Opaque here; created/destroyed by render_tree. */
struct statprov;
struct statprov *asp_statprov_create(const struct options *o);
void asp_statprov_destroy(struct statprov *sp);

/* Walk one root's subtree, emitting via the renderer. Prints the root line,
 * counts into *tot (root counts as a dir on success), adds to *errors. `sp` is
 * the shared stat backend (NULL = serial). */
void asp_walk(const char *root, const struct options *o, const struct renderer *r,
	      void *ctx, struct totals *tot, int *errors, struct statprov *sp,
	      int last_root);

/* Build a full in-memory entry tree for --asp-diff. The caller owns the arena
 * (arena_destroy to free all entries). Returns 0 on success, -1 if the root
 * could not be opened. */
struct asp_built_tree {
	struct entry **top;
	struct arena arena;
};
int asp_build_tree(const char *root, const struct options *o,
		   struct statprov *sp, struct asp_built_tree *out);

#endif /* ASP_TRAVERSE_H */
