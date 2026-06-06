/* aspen — entry point.
 *
 * Sprint 01: real traversal core reachable via the throwaway --asp-debug-walk
 * harness flag (used only by tests to verify discovery vs tree; never shipped
 * behavior). The option parser and default renderer land in Sprints 02-03.
 * Diagnostics always say "aspen"; see .docs/.
 */
#include <stdio.h>
#include <string.h>

#include "entry.h"
#include "sys/dir.h"
#include "traverse.h"
#include "version.h"

static void print_version(void)
{
	printf("%s v%s\n", ASP_PROGNAME, ASP_VERSION);
}

static void print_usage(FILE *out)
{
	fprintf(out, "usage: %s [options] [directory ...]\n", ASP_PROGNAME);
}

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

static void debug_visit(void *ctx, const struct entry *e, const char *fullpath,
			int depth, int is_last)
{
	(void)ctx;
	(void)depth;
	(void)is_last;
	printf("%c\t%s\n", type_char((enum asp_type)e->type), fullpath);
}

int main(int argc, char **argv)
{
	int debug_walk = 0;
	struct walk_opts opts = { 0 };
	const char *root = NULL;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			print_version();
			return 0;
		}
		if (!strcmp(argv[i], "--help")) {
			print_usage(stdout);
			return 0;
		}
		if (!strcmp(argv[i], "--asp-debug-walk")) {
			debug_walk = 1;
		} else if (!strcmp(argv[i], "--all")) {
			opts.all = 1;
		} else if (argv[i][0] != '-' && !root) {
			root = argv[i];
		}
	}

	if (debug_walk) {
		struct totals tot;
		int errors = asp_walk(root ? root : ".", &opts, debug_visit, NULL, &tot);
		fprintf(stderr, "[debug] %lu directories, %lu files, %d errors\n",
			tot.dirs, tot.files, errors);
		return errors ? 2 : 0;
	}

	/* Default rendering arrives in Sprint 02. */
	print_usage(stderr);
	return 0;
}
