#ifndef ASP_RENDER_H
#define ASP_RENDER_H

/*
 * Output renderer interface — a small vtable mirroring tree's listingcalls. The
 * default unix renderer (render/unix.c) is the hot path; JSON/XML/HTML slot in
 * at Sprint 09. The line/error/newline split lets a failed mid-tree directory
 * append "  [error opening dir]" before the newline, exactly like tree.
 */

#include "traverse.h"

/* Line-oriented renderers (unix, html): the engine walks the sorted DFS and calls
 * these as it goes. The error/newline split lets a failed mid-tree directory
 * append "  [error opening dir]" before the newline, exactly like tree. */
struct line_renderer {
	void (*begin)(void *ctx);                       /* document intro (noop for unix) */
	void (*root)(void *ctx, const char *path, const char *err,
		     const struct asp_statinfo *st);    /* root line; err!=NULL appended as "  [err]" */
	void (*entry)(void *ctx, const struct entry *e, const char *path,
		      int depth, int is_last);          /* indent + name + link, NO newline */
	void (*error)(void *ctx, const char *msg);      /* "  [<msg>]" */
	void (*newline)(void *ctx);
	void (*comment)(void *ctx, const struct entry *e, int depth); /* --info lines after the entry */
	void (*report)(void *ctx, const struct totals *t);
	void (*end)(void *ctx);                         /* document outtro */
};

/* Nested renderers (json, xml): the engine builds each root's whole subtree and
 * hands it off here to count + emit. tot is accumulated across roots. opened=0 ->
 * failed-open root (lstat-typed, "error opening dir"). limit_err != NULL -> a
 * directory root that tripped --filelimit (render it as a dir whose only content
 * is that error, counted as one directory). */
struct tree_renderer {
	void (*begin)(void *ctx);
	void (*tree)(void *ctx, const char *rootpath, const struct asp_statinfo *st,
		     int opened, const char *limit_err, struct entry **top,
		     struct totals *tot, int last_root);
	void (*report)(void *ctx, const struct totals *t);
	void (*end)(void *ctx);
};

/* A renderer is exactly one of the two kinds; the engine dispatches on which
 * pointer is non-NULL (`line` for unix/html, `tree` for json/xml). */
struct renderer {
	const struct line_renderer *line;
	const struct tree_renderer *tree;
};

/* Orchestrate over the root list: begin, per-root walk, report, end.
 * Returns the process exit code (0 ok, 2 if any open/stat failed). */
int render_tree(const char *const *dirs, const struct options *opts,
		const struct renderer *r, void *ctx, struct totals *tot);

#endif /* ASP_RENDER_H */
