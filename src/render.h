#ifndef ASP_RENDER_H
#define ASP_RENDER_H

/*
 * Output renderer interface — function-pointer vtables mirroring tree's
 * listingcalls, in two kinds: line-oriented (unix is the hot path, html) and
 * nested (json, xml). The line error/newline split lets a failed mid-tree
 * directory append "  [error opening dir]" before the newline, exactly like tree.
 */

#include "traverse.h"

struct options;

/* The document-level callbacks shared by BOTH renderer kinds. render_tree picks
 * begin/report/end from whichever vtable a renderer carries, so they must have
 * identical signatures across line_renderer and tree_renderer. Declaring them via
 * these typedefs (used in both structs) makes the compiler enforce that instead of
 * a comment (SR02-2.4). */
typedef void (*rdr_begin_fn)(void *ctx);                          /* document intro */
typedef void (*rdr_report_fn)(void *ctx, const struct totals *t); /* "N directories, M files" */
typedef void (*rdr_end_fn)(void *ctx);                            /* document outtro */

/* Line-oriented renderers (unix, html): the engine walks the sorted DFS and calls
 * these as it goes. The error/newline split lets a failed mid-tree directory
 * append "  [error opening dir]" before the newline, exactly like tree. */
struct line_renderer {
	rdr_begin_fn begin;                             /* document intro (noop for unix) */
	void (*root)(void *ctx, const char *path, const char *err,
		     const struct asp_statinfo *st);    /* root line; err!=NULL appended as "  [err]" */
	void (*entry)(void *ctx, const struct entry *e, const char *path,
		      int depth, int is_last, int rd);    /* indent + name + link, NO newline.
		      rd = tree's descend+htmldescend composite: 0 file, 1 descended dir,
		      >=2 a -R level boundary (html appends "/00Tree.html"). */
	void (*error)(void *ctx, const char *msg);      /* "  [<msg>]" */
	void (*newline)(void *ctx);
	void (*comment)(void *ctx, const struct entry *e, int depth); /* --info lines after the entry */
	rdr_report_fn report;
	rdr_end_fn end;                                 /* document outtro */
};

/* Nested renderers (json, xml): the engine builds each root's whole subtree and
 * hands it off here to count + emit. tot is accumulated across roots. opened=0 ->
 * failed-open root (lstat-typed, "error opening dir"). limit_err != NULL -> a
 * directory root that tripped --filelimit (render it as a dir whose only content
 * is that error, counted as one directory). */
struct tree_renderer {
	rdr_begin_fn begin;
	void (*tree)(void *ctx, const char *rootpath, const struct asp_statinfo *st,
		     int opened, const char *limit_err, struct entry **top,
		     struct totals *tot, int last_root);
	rdr_report_fn report;
	rdr_end_fn end;
};

/* A renderer is exactly one of the two kinds; the engine dispatches on which
 * pointer is non-NULL (`line` for unix/html, `tree` for json/xml). */
struct renderer {
	const struct line_renderer *line;
	const struct tree_renderer *tree;
	/* -R "rerun": at a -L level boundary, re-render `path`'s subtree as a fresh
	 * document written to `path`/00Tree.html (tree's setoutput+emit_tree). Set for
	 * the line renderers (unix text, html); NULL for json/xml (no rerun there). */
	void (*rerun)(void *ctx, const char *path, const struct options *o);
};

/* Orchestrate over the root list: begin, per-root walk, report, end.
 * Returns the process exit code (0 ok, 2 if any open/stat failed). */
int render_tree(const char *const *dirs, const struct options *opts,
		const struct renderer *r, void *ctx, struct totals *tot);

#endif /* ASP_RENDER_H */
