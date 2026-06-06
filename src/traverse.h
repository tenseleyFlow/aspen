#ifndef ASP_TRAVERSE_H
#define ASP_TRAVERSE_H

/*
 * Depth-first traversal engine (serial). fd-relative (openat/getdents/fstatat),
 * arena-backed entries, stat only when forced. Emits each entry to a renderer in
 * pre-order; the renderer draws indentation from depth + is_last + ancestor
 * last-flags it tracks. Descent decisions (type, and later -L/-d/-x/-l) live here.
 */

#include "entry.h"

struct totals {
	unsigned long dirs;
	unsigned long files;
};

struct walk_opts {
	int all;            /* -a: include dotfiles */
	int follow_links;   /* -l (Sprint 03) */
	int one_fs;         /* -x (Sprint 03) */
	unsigned stat_mask; /* metadata flags that force a stat (Sprint 04) */
};

struct renderer; /* defined in render.h */

/* Walk one root's subtree, emitting via the renderer. Prints the root line
 * (r->root), counts entries into *tot (root counts as a dir on success), and
 * adds to *errors. */
void asp_walk(const char *root, const struct walk_opts *opts,
	      const struct renderer *r, void *ctx, struct totals *tot, int *errors);

#endif /* ASP_TRAVERSE_H */
