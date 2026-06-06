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

/* Walk one root's subtree, emitting via the renderer. Prints the root line,
 * counts into *tot (root counts as a dir on success), adds to *errors. */
void asp_walk(const char *root, const struct options *o, const struct renderer *r,
	      void *ctx, struct totals *tot, int *errors, int last_root);

#endif /* ASP_TRAVERSE_H */
