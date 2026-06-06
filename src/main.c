/* aspen — entry point.
 *
 * Sprint 02: default (unix) output, byte-identical to tree. The full option
 * parser is Sprint 03; here main does a minimal parse (roots, --charset, the
 * --asp-debug-walk test hook). Diagnostics always say "aspen"; see .docs/.
 */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "entry.h"
#include "render.h"
#include "render/unix.h"
#include "sys/dir.h"
#include "traverse.h"
#include "util.h"
#include "version.h"

static void print_version(void)
{
	printf("%s v%s\n", ASP_PROGNAME, ASP_VERSION);
}

static void print_usage(FILE *out)
{
	fprintf(out, "usage: %s [options] [directory ...]\n", ASP_PROGNAME);
}

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

static void dbg_begin_end(void *c) { (void)c; }
static void dbg_root(void *c, const char *p, int f) { (void)c; (void)p; (void)f; }
static void dbg_error(void *c, const char *m) { (void)c; (void)m; }
static void dbg_newline(void *c) { (void)c; }
static void dbg_report(void *c, const struct totals *t) { (void)c; (void)t; }
static void dbg_entry(void *c, const struct entry *e, const char *path, int depth, int last)
{
	(void)c;
	(void)depth;
	(void)last;
	printf("%c\t%s\n", type_char((enum asp_type)e->type), path);
}
static const struct renderer DEBUG_RENDERER = {
	dbg_begin_end, dbg_root, dbg_entry, dbg_error, dbg_newline, dbg_report, dbg_begin_end,
};

int main(int argc, char **argv)
{
	setlocale(LC_CTYPE, "");
	setlocale(LC_COLLATE, "");
	int mb = (int)MB_CUR_MAX;

	int debug_walk = 0;
	struct walk_opts opts = { 0 };
	const char *charset = NULL;
	const char **roots = asp_xmalloc((size_t)(argc + 2) * sizeof *roots);
	int nr = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--version")) {
			print_version();
			free(roots);
			return 0;
		} else if (!strcmp(a, "--help")) {
			print_usage(stdout);
			free(roots);
			return 0;
		} else if (!strcmp(a, "--asp-debug-walk")) {
			debug_walk = 1;
		} else if (!strcmp(a, "--all")) {
			opts.all = 1;
		} else if (!strncmp(a, "--charset=", 10)) {
			charset = a + 10;
		} else if (!strcmp(a, "--charset") && i + 1 < argc) {
			charset = argv[++i];
		} else if (a[0] != '-') {
			roots[nr++] = argv[i];
		}
		/* unknown flags: ignored until the Sprint 03 parser */
	}

	if (nr == 0)
		roots[nr++] = ".";
	roots[nr] = NULL;

	int rc;
	if (debug_walk) {
		struct totals tot;
		rc = render_tree((const char *const *)roots, &opts, &DEBUG_RENDERER, NULL, &tot);
		fprintf(stderr, "[debug] %lu directories, %lu files\n", tot.dirs, tot.files);
	} else {
		struct unix_ctx u;
		unix_ctx_init(&u, STDOUT_FILENO, mb, charset);
		rc = render_tree((const char *const *)roots, &opts, &asp_unix_renderer, &u, NULL);
		unix_ctx_destroy(&u);
	}

	free(roots);
	return rc;
}
