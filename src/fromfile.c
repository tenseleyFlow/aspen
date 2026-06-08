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


static struct fnode *newnode(const char *name)
{
	struct fnode *n = asp_xmalloc(sizeof *n);
	n->child = n->next = NULL;
	n->name = asp_strdup(name);
	n->lnk = NULL;
	n->isdir = 0;
	n->islink = 0;
	n->ctail = NULL;
	n->cidx = NULL;
	return n;
}

/* Per-parent child index: an open-addressed name->fnode hash so fsearch is O(1)
 * amortized instead of tree's O(N^2) linear sibling scan on large flat input.
 * Build-only; freed by fchildren_free once the tree is read. */
struct fchildren {
	struct fnode **slot; /* power-of-two; NULL = empty */
	size_t cap, len;
};

static unsigned long fname_hash(const char *s)
{
	unsigned long h = 1469598103934665603UL; /* FNV-1a */
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h *= 1099511628211UL;
	}
	return h;
}

static struct fchildren *fchildren_new(void)
{
	struct fchildren *c = asp_xmalloc(sizeof *c);
	c->cap = 16;
	c->len = 0;
	c->slot = asp_xmalloc(sizeof *c->slot * c->cap);
	memset(c->slot, 0, sizeof *c->slot * c->cap);
	return c;
}

static void fchildren_free(void *p)
{
	struct fchildren *c = p;
	if (!c)
		return;
	free(c->slot);
	free(c);
}

static void fchildren_grow(struct fchildren *c)
{
	size_t ncap = c->cap * 2, mask = ncap - 1;
	struct fnode **ns = asp_xmalloc(sizeof *ns * ncap);
	memset(ns, 0, sizeof *ns * ncap);
	for (size_t i = 0; i < c->cap; i++) {
		if (!c->slot[i])
			continue;
		size_t j = fname_hash(c->slot[i]->name) & mask;
		while (ns[j])
			j = (j + 1) & mask;
		ns[j] = c->slot[i];
	}
	free(c->slot);
	c->slot = ns;
	c->cap = ncap;
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

/* tree's search(): find `name` among parent's children, reusing on match (shared
 * prefixes), else append at the tail in insertion order (sort happens later).
 * O(1) amortized via parent->cidx instead of tree's O(N^2) linear scan. */
static struct fnode *fsearch(struct fnode *parent, const char *name)
{
	struct fchildren *c = parent->cidx;
	if (!c)
		c = parent->cidx = fchildren_new();
	size_t mask = c->cap - 1;
	size_t i = fname_hash(name) & mask;
	while (c->slot[i]) {
		if (strcmp(c->slot[i]->name, name) == 0)
			return c->slot[i]; /* reuse (shared prefix) */
		i = (i + 1) & mask;
	}
	struct fnode *n = newnode(name);
	if (parent->ctail)
		parent->ctail->next = n;
	else
		parent->child = n;
	parent->ctail = n;
	if ((c->len + 1) * 10 >= c->cap * 7) { /* keep load < 0.7; reprobe after grow */
		fchildren_grow(c);
		mask = c->cap - 1;
		i = fname_hash(name) & mask;
		while (c->slot[i])
			i = (i + 1) & mask;
	}
	c->slot[i] = n;
	c->len++;
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
	struct fnode root = { 0 }; /* dummy parent so the top list has a cidx/ctail too */
	while (fgets(buf, MAXPATH, fp) != NULL) {
		if (is_comment(buf))
			continue;
		size_t len;
		strip_eol(buf, &len);
		if (len == 0)
			continue;

		char *spath = buf;
		struct fnode *cwd = &root;
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
				cwd = ent;
			}
		} while (tok != T_FILE && tok != T_EOP);

		if (ent && link) {
			ent->isdir = 0;
			ent->islink = 1;
			free(ent->lnk);
			ent->lnk = asp_strdup(link);
		}
	}
	fchildren_free(root.cidx);
	return root.child;
}

/* --fromtabfile: leading tabs give depth; istack tracks the parent per level. */
static struct fnode *read_tabs(FILE *fp, const struct options *o, char *buf)
{
	struct fnode root = { 0 }; /* dummy parent for the top-level list */
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

		struct fnode *ent = fsearch(tabs ? istack[tabs - 1] : &root, spath);
		istack[tabs] = ent;
		if (tabs)
			istack[tabs - 1]->isdir = 1;
		if (link) {
			ent->isdir = 0;
			ent->islink = 1;
			free(ent->lnk);
			ent->lnk = asp_strdup(link);
		}
		top_depth = tabs;
	}
	free(istack);
	fchildren_free(root.cidx);
	return root.child;
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
		fchildren_free(top->cidx); /* build-only index, if not already freed */
		free(top->name);
		free(top->lnk);
		free(top);
		top = next;
	}
}
