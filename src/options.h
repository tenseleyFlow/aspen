#ifndef ASP_OPTIONS_H
#define ASP_OPTIONS_H

/*
 * Option state + tree-compatible parser. The parser recognizes tree's entire
 * flag surface (so a valid tree flag never spuriously errors), storing each into
 * this struct; behavior is wired in per sprint. Fields are grouped by the sprint
 * that consumes them. Diagnostics say "aspen" and exit(1) on parse error, with
 * tree's wording otherwise.
 */

#include <stddef.h>

enum sort_kind {
	SORT_NAME = 0,
	SORT_VERSION,
	SORT_SIZE,
	SORT_MTIME,
	SORT_CTIME,
	SORT_NONE,
};

enum out_format { OUT_UNIX = 0, OUT_XML, OUT_JSON, OUT_HTML };

struct options {
	/* listing (Sprint 03) */
	int all, dirsonly, fullpath, noindent, xdev, follow, classify;
	int rerun; /* -R: parsed and accepted; the HTML 00Tree.html rerun action is
		    * a deferred feature (tree's -R is inert without -L anyway). */
	long level; /* -L; -1 = unlimited */

	/* file info (Sprint 04) */
	int sizeflag, humanflag, siflag, duflag;
	int permflag, userflag, groupflag, dateflag, ctimeflag;
	int inodeflag, devflag;
	const char *timefmt;

	/* patterns / filtering (Sprint 06/08) */
	int ignorecase, matchdirs;
	int quote, noprint, qmark; /* -Q / -N / -q */
	const char **patterns; size_t npat, patcap;
	const char **ipatterns; size_t nipat, ipatcap;
	int gitignore; const char *gitfile;
	int prune, showinfo; const char *infofile;
	long filelimit;
	int metafirst;
	int condense; /* --condense: collapse singleton dir chains onto one line */

	/* sorting (Sprint 05) */
	int reverse, dirsfirst, filesfirst;
	enum sort_kind sort;

	/* graphics / color (Sprint 02/07) */
	const char *charset;
	int ansilines, forcecolor, nocolor, noreport, hyperlink;
	const char *scheme, *authority;

	/* output format (Sprint 09) */
	enum out_format format;
	const char *host, *title, *hintro, *houtro, *outfilename;
	int nolinks, htmloffset;

	/* input (Sprint 10) */
	int fromfile, fromtabfile, fflinks;

	/* concurrency (Sprint 11b): worker threads for the metadata stat pass.
	 * 0 = auto (size to cores); 1 = force serial; N = N workers. Output is
	 * byte-identical regardless — only stat() is parallelized. */
	int threads;

	/* derived (set after parse / color_init). The per-entry stat decision is NOT
	 * a precomputed mask — it is computed per context by meta_wanted() /
	 * sort_needs_stat() / nonlink_needs_stat() in traverse.c, which already fold
	 * in d_type and -l follow. */
	int colorize; /* whether colorization is active (forces stat for mode) */
};

void options_init(struct options *o);

/* Parse argv into *o; collect roots into *roots (NULL-terminated, caller frees).
 * Exits(1) with a tree-style message on a parse error. */
void options_parse(int argc, char **argv, struct options *o,
		   const char ***roots, int *nroots);

#endif /* ASP_OPTIONS_H */
