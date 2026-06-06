#ifndef ASP_FILTER_H
#define ASP_FILTER_H

/*
 * .gitignore filtering (tree's filter.c). A stack of loaded ignore files
 * (pushed per directory on descent, popped on ascent); an entry is filtered
 * when a remove pattern matches and no reverse (!) pattern does. Relative
 * patterns (no '/', or only trailing '/') match the basename at any level;
 * absolute patterns match the path relative to the ignore file's directory
 * (the 2.3.2 semantics).
 *
 * NOT YET: the implicit parent-.gitignore search up to a .git root
 * (gitignore_search) — see plan.md ledger.
 */

struct gpattern {
	char *pattern;  /* leading '/' stripped */
	int relative;
	struct gpattern *next;
};

struct ignorefile {
	char *path; /* directory the patterns are relative to */
	struct gpattern *remove;
	struct gpattern *reverse;
	struct ignorefile *next;
};

/* Load <dirpath>/.gitignore, or NULL if none. */
struct ignorefile *gitignore_load_dir(const char *dirpath);
/* Load an explicit ignore file (basepath = dir its patterns are relative to). */
struct ignorefile *gitignore_load_file(const char *basepath, const char *filepath);

void gitstack_push(struct ignorefile **stack, struct ignorefile *ig);
void gitstack_pop(struct ignorefile **stack);
void gitstack_flush(struct ignorefile **stack);

/* 1 if the entry is filtered out (ignored), else 0. ic = --ignore-case. */
int gitignore_filtered(struct ignorefile *stack, const char *path,
		       const char *name, int isdir, int ic);

#endif /* ASP_FILTER_H */
