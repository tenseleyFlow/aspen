#include "fromfile.h"
#include "util.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tree's defaults (file.c): comment marker and path separator. */
#define FILE_COMMENT "#"
#define FILE_PATHSEP "/"
#define MAXPATH (64 * 1024)  /* tree's 64K line cap */
#define MAXDEPTH 2048        /* tree's tab-depth cap */

enum ftok { T_PATHSEP, T_DIR, T_FILE, T_EOP };

static char *xdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = asp_xmalloc(n);
	memcpy(p, s, n);
	return p;
}

static struct fnode *newnode(const char *name)
{
	struct fnode *n = asp_xmalloc(sizeof *n);
	n->child = n->next = NULL;
	n->name = xdup(name);
	n->lnk = NULL;
	n->isdir = 0;
	n->islink = 0;
	return n;
}

/*
 * tree's path tokenizer (file.c:nextpc). Splits *p in place on FILE_PATHSEP,
 * null-terminating each component. The static `prev` carries the separator that
 * followed a T_DIR so the next call returns a T_PATHSEP — mirrored exactly,
 * including the harmless leak across lines (a leading T_PATHSEP is a no-op).
 */
static char *nextpc(char **p, int *tok)
{
	static char prev = 0;
	char *s = *p;
	if (!**p) {
		*tok = T_EOP;
		return NULL;
	}
	if (prev) {
		prev = 0;
		*tok = T_PATHSEP;
		return NULL;
	}
	if (strchr(FILE_PATHSEP, **p) != NULL) {
		(*p)++;
		*tok = T_PATHSEP;
		return NULL;
	}
	while (**p && strchr(FILE_PATHSEP, **p) == NULL)
		(*p)++;
	if (**p) {
		*tok = T_DIR;
		prev = **p;
		*(*p)++ = '\0';
	} else {
		*tok = T_FILE;
	}
	return s;
}

/* tree's search(): find `name` in the sibling list, reusing on match (shared
 * prefixes), else append at the tail in insertion order (sort happens later). */
static struct fnode *fsearch(struct fnode **list, const char *name)
{
	if (*list == NULL)
		return (*list = newnode(name));
	struct fnode *ptr, *prev = *list;
	for (ptr = *list; ptr != NULL; ptr = ptr->next) {
		if (strcmp(ptr->name, name) == 0)
			return ptr;
		prev = ptr;
	}
	struct fnode *n = newnode(name);
	prev->next = n;
	return n;
}

static void strip_eol(char *s, size_t *len)
{
	size_t l = strlen(s);
	while (l && (s[l - 1] == '\n' || s[l - 1] == '\r'))
		s[--l] = '\0';
	*len = l;
}

static int is_comment(const char *line)
{
	return strncmp(line, FILE_COMMENT, strlen(FILE_COMMENT)) == 0;
}

/* --fromfile: newline-separated paths, tokenized into a shared hierarchy. */
static struct fnode *read_paths(FILE *fp, const struct options *o, char *buf)
{
	struct fnode *top = NULL;
	while (fgets(buf, MAXPATH, fp) != NULL) {
		if (is_comment(buf))
			continue;
		size_t len;
		strip_eol(buf, &len);
		if (len == 0)
			continue;

		char *spath = buf;
		struct fnode **cwd = &top;
		char *link = o->fflinks ? strstr(buf, " -> ") : NULL;
		if (link) {
			*link = '\0';
			link += 4;
		}
		struct fnode *ent = NULL;
		int tok;
		do {
			char *s = nextpc(&spath, &tok);
			if (tok == T_PATHSEP)
				continue;
			if (tok == T_FILE || tok == T_DIR) {
				if (strcmp(s, ".") == 0)
					continue;
				ent = fsearch(cwd, s);
				if (tok == T_DIR)
					ent->isdir = 1;
				cwd = &ent->child;
			}
		} while (tok != T_FILE && tok != T_EOP);

		if (ent && link) {
			ent->isdir = 0;
			ent->islink = 1;
			free(ent->lnk);
			ent->lnk = xdup(link);
		}
	}
	return top;
}

/* --fromtabfile: leading tabs give depth; istack tracks the parent per level. */
static struct fnode *read_tabs(FILE *fp, const struct options *o, char *buf)
{
	struct fnode *top = NULL;
	struct fnode **istack = asp_xmalloc(sizeof *istack * MAXDEPTH);
	memset(istack, 0, sizeof *istack * MAXDEPTH);
	size_t line = 0, top_depth = 0;

	while (fgets(buf, MAXPATH, fp) != NULL) {
		line++;
		if (is_comment(buf))
			continue;
		size_t len;
		strip_eol(buf, &len);
		if (len == 0)
			continue;

		size_t tabs = 0;
		while (buf[tabs] == '\t')
			tabs++;
		if (tabs >= MAXDEPTH) {
			fprintf(stderr,
				"%s: Tab depth exceeds maximum path depth (%zu >= %d) on line %zu\n",
				ASP_PROGNAME, tabs, MAXDEPTH, line);
			continue;
		}

		char *spath = buf + tabs;
		char *link = o->fflinks ? strstr(spath, " -> ") : NULL;
		if (link) {
			*link = '\0';
			link += 4;
		}
		if (tabs > 0 && ((tabs - 1 > top_depth) || istack[tabs - 1] == NULL)) {
			fprintf(stderr,
				"%s: Orphaned file [%s] on line %zu, check tab depth in file.\n",
				ASP_PROGNAME, spath, line);
			continue;
		}

		struct fnode *ent = fsearch(tabs ? &istack[tabs - 1]->child : &top, spath);
		istack[tabs] = ent;
		if (tabs)
			istack[tabs - 1]->isdir = 1;
		if (link) {
			ent->isdir = 0;
			ent->islink = 1;
			free(ent->lnk);
			ent->lnk = xdup(link);
		}
		top_depth = tabs;
	}
	free(istack);
	return top;
}

struct fnode *asp_fromfile_read(const char *arg, const struct options *o,
				int tabbed, int *open_err)
{
	*open_err = 0;
	FILE *fp = strcmp(arg, ".") != 0 ? fopen(arg, "r") : stdin;
	if (fp == NULL) {
		*open_err = 1;
		return NULL;
	}
	char *buf = asp_xmalloc(MAXPATH);
	struct fnode *top = tabbed ? read_tabs(fp, o, buf) : read_paths(fp, o, buf);
	free(buf);
	if (fp != stdin)
		fclose(fp);
	return top;
}

void asp_fnode_free(struct fnode *top)
{
	while (top) {
		struct fnode *next = top->next;
		asp_fnode_free(top->child);
		free(top->name);
		free(top->lnk);
		free(top);
		top = next;
	}
}
