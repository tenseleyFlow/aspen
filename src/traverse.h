#ifndef ASP_TRAVERSE_H
#define ASP_TRAVERSE_H

/*
 * Depth-first traversal engine (serial). fd-relative (openat/getdents/fstatat),
 * arena-backed entries, stat only when forced. Emits each entry to a visitor in
 * pre-order; the visitor (debug dump now, the real renderer in Sprint 02) draws
 * indentation from depth + is_last. Descent decisions (type, and later -L/-d/-x/-l)
 * live here, not in the visitor.
 */

#include "entry.h"

struct totals {
	unsigned long dirs;
	unsigned long files;
};

struct walk_opts {
	int all;          /* -a: include dotfiles */
	int follow_links; /* -l (Sprint 03) */
	int one_fs;       /* -x (Sprint 03) */
	unsigned stat_mask; /* metadata flags that force a stat (Sprint 04) */
};

/* Called once per discovered entry, pre-order. fullpath is the entry's path
 * (root joined with components). Do not retain pointers past the call. */
typedef void (*asp_visit_fn)(void *ctx, const struct entry *e,
			     const char *fullpath, int depth, int is_last);

/* Walk `root`'s subtree. Returns the number of errors (failed opens/stats),
 * mirroring tree's error accounting (=> exit code 2 when nonzero). */
int asp_walk(const char *root, const struct walk_opts *opts,
	     asp_visit_fn visit, void *ctx, struct totals *tot);

#endif /* ASP_TRAVERSE_H */
