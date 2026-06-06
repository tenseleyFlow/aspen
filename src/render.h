#ifndef ASP_RENDER_H
#define ASP_RENDER_H

/*
 * Output renderer interface — a small vtable mirroring tree's listingcalls. The
 * default unix renderer (render/unix.c) is the hot path; JSON/XML/HTML slot in
 * at Sprint 09. The line/error/newline split lets a failed mid-tree directory
 * append "  [error opening dir]" before the newline, exactly like tree.
 */

#include "traverse.h"

struct renderer {
	void (*begin)(void *ctx);                       /* document intro (noop for unix) */
	void (*root)(void *ctx, const char *path, int failed,
		     const struct asp_statinfo *st);    /* root line (+ bracket / error) */
	void (*entry)(void *ctx, const struct entry *e, const char *path,
		      int depth, int is_last);          /* indent + name + link, NO newline */
	void (*error)(void *ctx, const char *msg);      /* "  [<msg>]" */
	void (*newline)(void *ctx);
	void (*report)(void *ctx, const struct totals *t);
	void (*end)(void *ctx);                         /* document outtro */
};

/* Orchestrate over the root list: begin, per-root walk, report, end.
 * Returns the process exit code (0 ok, 2 if any open/stat failed). */
int render_tree(const char *const *dirs, const struct options *opts,
		const struct renderer *r, void *ctx, struct totals *tot);

#endif /* ASP_RENDER_H */
