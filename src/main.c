/* aspen — entry point.
 *
 * Skeleton (Sprint 00): wires --version/--help and the program-name policy.
 * The traversal engine and option parser land in Sprints 01–02, ported from the
 * validated POC. Diagnostics always say "aspen" (never "tree"); see .docs/.
 */
#include <stdio.h>
#include <string.h>

#include "version.h"

static void print_version(void)
{
	printf("%s v%s\n", ASP_PROGNAME, ASP_VERSION);
}

static void print_usage(FILE *out)
{
	fprintf(out, "usage: %s [options] [directory ...]\n", ASP_PROGNAME);
}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			print_version();
			return 0;
		}
		if (!strcmp(argv[i], "--help")) {
			print_usage(stdout);
			return 0;
		}
	}

	/* Real behavior arrives in Sprint 01–02 (traversal + default output). */
	print_usage(stderr);
	return 0;
}
