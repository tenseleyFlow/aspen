/* aspen — entry point.
 *
 * Sprint 03: full option parser + listing flags. main filters the test-only
 * --asp-debug-walk hook, parses the rest with the tree-compatible parser, and
 * dispatches to the unix renderer. Diagnostics say "aspen"; see .docs/.
 */
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "version.h"

#include "color.h"
#include "entry.h"
#include "idcache.h"
#include "options.h"
#include "render.h"
#include "render/html.h"
#include "render/json.h"
#include "render/unix.h"
#include "render/xml.h"
#include "sys/dir.h"
#include "traverse.h"
#include "util.h"

/* --- throwaway discovery renderer for --asp-debug-walk (tests only) --- */

static char type_char(enum asp_type t)
{
	switch (t) {
	case ASP_DIR:  return 'd';
	case ASP_REG:  return 'f';
	case ASP_LNK:  return 'l';
	case ASP_FIFO: return 'p';
	case ASP_SOCK: return 's';
	case ASP_CHR:  return 'c';
	case ASP_BLK:  return 'b';
	case ASP_WHT:  return 'w';
	default:       return 'u';
	}
}

static void dbg_noop(void *c) { (void)c; }
static void dbg_root(void *c, const char *p, int f, const struct asp_statinfo *st)
{
	(void)c; (void)p; (void)f; (void)st;
}
static void dbg_error(void *c, const char *m) { (void)c; (void)m; }
static void dbg_newline(void *c) { (void)c; }
static void dbg_comment(void *c, const struct entry *e, int d) { (void)c; (void)e; (void)d; }
static void dbg_report(void *c, const struct totals *t) { (void)c; (void)t; }
static void dbg_entry(void *c, const struct entry *e, const char *path, int depth, int last)
{
	(void)c;
	(void)depth;
	(void)last;
	printf("%c\t%s\n", type_char((enum asp_type)e->type), path);
}
static const struct renderer DEBUG_RENDERER = {
	dbg_noop, dbg_root, dbg_entry, dbg_error, dbg_newline, dbg_comment, dbg_report, dbg_noop,
	NULL,
};

int main(int argc, char **argv)
{
	setlocale(LC_CTYPE, "");
	setlocale(LC_COLLATE, "");
	int mb = (int)MB_CUR_MAX;

	/* The walk uses one open dir fd per active depth — fd-relative openat is
	 * the #1 speed lever (no path re-resolution), but it bounds depth by the
	 * open-file limit. Raise the soft limit to the hard limit so aspen descends
	 * as deep as the system permits, far past tree's PATH_MAX cutoff (~500).
	 * Only when the soft limit is low enough to actually constrain a deep walk:
	 * a generous soft limit needs no bump, so trivial invocations skip these two
	 * syscalls (keeps tiny listings at/under tree's startup cost — SR-0.2). */
	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max &&
	    rl.rlim_cur < 131072) {
		rl.rlim_cur = rl.rlim_max;
		setrlimit(RLIMIT_NOFILE, &rl);
	}

	/* Pull out the test-only debug hook; parse everything else as tree flags. */
	char **fav = asp_xmalloc((size_t)(argc + 1) * sizeof *fav);
	int fac = 0, debug = 0;
	fav[fac++] = argv[0];
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--asp-debug-walk"))
			debug = 1;
		else
			fav[fac++] = argv[i];
	}
	fav[fac] = NULL;

	struct options o;
	options_init(&o);
	const char **roots;
	int nr;
	options_parse(fac, fav, &o, &roots, &nr);
	if (nr == 0) {
		roots[0] = ".";
		roots[1] = NULL;
	}

	/* -o FILE: write output to a file instead of stdout (diagnostics stay on
	 * stderr). tree's wording + exit on open failure. */
	int outfd = STDOUT_FILENO;
	if (o.outfilename) {
		outfd = open(o.outfilename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (outfd < 0) {
			fprintf(stderr, "%s: invalid filename '%s'\n", ASP_PROGNAME,
				o.outfilename);
			free(fav);
			free((void *)roots);
			return 1;
		}
	}

	int rc;
	if (debug) {
		struct totals t;
		rc = render_tree(roots, &o, &DEBUG_RENDERER, NULL, &t);
		fprintf(stderr, "[debug] %lu directories, %lu files\n", t.dirs, t.files);
	} else if (o.format == OUT_JSON) {
		struct json_ctx j;
		json_ctx_init(&j, outfd, &o);
		rc = render_tree(roots, &o, &asp_json_renderer, &j, NULL);
		json_ctx_destroy(&j);
	} else if (o.format == OUT_XML) {
		struct xml_ctx x;
		xml_ctx_init(&x, outfd, &o);
		rc = render_tree(roots, &o, &asp_xml_renderer, &x, NULL);
		xml_ctx_destroy(&x);
	} else if (o.format == OUT_HTML) {
		/* -C in HTML emits a per-entry class (incl. EXEC), which needs the exec
		 * bit; flag colorize so the traversal stats entries (tree always does). */
		o.colorize = o.forcecolor;
		struct html_ctx hc;
		html_ctx_init(&hc, outfd, mb, &o);
		rc = render_tree(roots, &o, &asp_html_renderer, &hc, NULL);
		html_ctx_destroy(&hc);
	} else {
		struct colorizer col;
		color_init(&col, &o, outfd);
		o.colorize = col.enabled;
		struct unix_ctx u;
		unix_ctx_init(&u, outfd, mb, &o, &col);
		rc = render_tree(roots, &o, &asp_unix_renderer, &u, NULL);
		unix_ctx_destroy(&u);
		color_free(&col);
	}

	if (outfd != STDOUT_FILENO)
		close(outfd);
	idcache_free(); /* clean shutdown: release the -u/-g name caches */
	free(fav);
	free((void *)roots);
	return rc;
}
