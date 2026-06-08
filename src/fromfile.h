#ifndef ASP_FROMFILE_H
#define ASP_FROMFILE_H

/*
 * --fromfile / --fromtabfile input reconstruction (mirrors tree's file.c).
 *
 * Parses a path list (newline-separated paths) or a tab-indented tree from a
 * file (or stdin when the argument is ".") into a synthetic hierarchy of
 * `fnode`s. The traverse layer converts these into arena `entry`s and feeds the
 * normal renderers, so every output format works on reconstructed trees.
 *
 * Shared-prefix paths reuse nodes (tree's search()); non-final components are
 * directories. With --fflinks a trailing " -> target" marks a symlink.
 */

#include "options.h"

struct fnode {
	struct fnode *child; /* first child (insertion order; sorted later) */
	struct fnode *next;  /* next sibling */
	char *name;
	char *lnk;           /* --fflinks target, or NULL */
	int isdir;
	int islink;
	/* build-only: O(1) sibling dedup/append (freed after read). ctail is the
	 * child list's tail; cidx is a name->child hash. */
	struct fnode *ctail;
	void *cidx;
};

/*
 * Read `arg` ("." => stdin) into a top-level sibling list. tabbed selects the
 * tab-indented format. *open_err is set to 1 if the file could not be opened.
 * Orphan / tab-depth diagnostics are written to stderr as "aspen: ...".
 */
struct fnode *asp_fromfile_read(const char *arg, const struct options *o,
				int tabbed, int *open_err);

void asp_fnode_free(struct fnode *top);

#endif /* ASP_FROMFILE_H */
