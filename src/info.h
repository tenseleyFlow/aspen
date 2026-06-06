#ifndef ASP_INFO_H
#define ASP_INFO_H

/*
 * .info annotation files (tree's info.c). Format: '#' comments, pattern lines,
 * then tab-indented description lines. A stack of loaded .info files (per
 * directory + an explicit --infofile / global). info_check returns the matching
 * description lines for an entry.
 */

#include "filter.h" /* struct gpattern */

struct icomment {
	struct gpattern *pattern;
	char **desc; /* NULL-terminated description lines */
	struct icomment *next;
};

struct infofile {
	char *path; /* directory/file the patterns are relative to */
	struct icomment *comments;
	struct infofile *next;
};

struct infofile *info_load_dir(const char *dirpath);  /* <dirpath>/.info */
struct infofile *info_load_file(const char *path);    /* explicit file */

void infostack_push(struct infofile **stack, struct infofile *inf);
void infostack_pop(struct infofile **stack);
void infostack_flush(struct infofile **stack);

/* Matching description lines (NULL-terminated), or NULL. top = the current dir
 * has a .info (match basename too). ic = --ignore-case. */
char **info_check(struct infofile *stack, const char *path, const char *name,
		  int top, int isdir, int ic);

#endif /* ASP_INFO_H */
