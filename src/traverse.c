#include "traverse.h"
#include "render.h"
#include "arena.h"
#include "dstr.h"
#include "entry.h"
#include "util.h"
#include "sys/dir.h"
#include "sys/xstat.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct wctx {
	struct arena arena;
	struct dstr path; /* current path, no trailing slash */
	const struct walk_opts *opts;
	const struct renderer *r;
	void *rctx;
	struct totals *tot;
	int *errors;
};

struct evec {
	struct entry **v;
	size_t n, cap;
};

static int cmp_name(const void *a, const void *b)
{
	const struct entry *x = *(const struct entry *const *)a;
	const struct entry *y = *(const struct entry *const *)b;
	return strcoll(x->name, y->name);
}

static void evec_push(struct evec *ev, struct entry *e)
{
	if (ev->n == ev->cap) {
		ev->cap = ev->cap ? ev->cap * 2 : 32;
		ev->v = asp_xrealloc(ev->v, ev->cap * sizeof *ev->v);
	}
	ev->v[ev->n++] = e;
}

static int needs_stat(enum asp_type t, const struct walk_opts *o)
{
	if (t == ASP_UNKNOWN)
		return 1;
	if (o->stat_mask)
		return 1;
	if (o->one_fs)
		return 1;
	if (o->follow_links && t == ASP_LNK)
		return 1;
	return 0;
}

/* Fill e->lnk with the symlink target (or tree's exact error string). readlink
 * is required output data, not a stat — it does not break the no-stat fast path. */
static void fill_link(struct wctx *c, int dirfd, struct entry *e)
{
	char buf[4096];
	ssize_t n = readlinkat(dirfd, e->name, buf, sizeof buf - 1);
	if (n < 0) {
		e->lnk = arena_strdup(&c->arena, "[Error reading symbolic link information]");
		return;
	}
	buf[n] = '\0';
	e->lnk = arena_strdup(&c->arena, buf);
}

static void walk_dir(struct wctx *c, struct asp_dir *d, int depth)
{
	struct arena_marker mk = arena_mark(&c->arena);
	size_t pathlen = c->path.len;
	struct evec ev = { NULL, 0, 0 };
	struct asp_dirent de;
	int r;

	while ((r = asp_dirread(d, &de)) == 1) {
		if (!c->opts->all && de.name[0] == '.')
			continue;

		enum asp_type t = de.type;
		if (needs_stat(t, c->opts)) {
			struct asp_statinfo si;
			if (asp_stat_at(asp_dirfd(d), de.name, 0, &si) == 0) {
				if (t == ASP_UNKNOWN)
					t = asp_type_from_mode(si.mode);
			} else {
				(*c->errors)++;
				continue;
			}
		}

		struct entry *e = entry_new(&c->arena, de.name, strlen(de.name), t);
		if (t == ASP_LNK)
			fill_link(c, asp_dirfd(d), e);
		evec_push(&ev, e);
	}
	if (r < 0)
		(*c->errors)++;

	/* Default alphabetical sort (tree's alnumsort = strcoll). The other sort
	 * modes (-v/-t/-c/-U/-r, dirsfirst) and the strxfrm key-cache optimization
	 * are Sprint 05; the default ordering is required for default parity. */
	qsort(ev.v, ev.n, sizeof *ev.v, cmp_name);

	for (size_t i = 0; i < ev.n; i++) {
		struct entry *e = ev.v[i];
		int is_last = (i + 1 == ev.n);

		dstr_appendc(&c->path, '/');
		dstr_append(&c->path, e->name, e->namelen);

		if (e->type == ASP_DIR)
			c->tot->dirs++;
		else
			c->tot->files++;

		c->r->entry(c->rctx, e, c->path.data, depth, is_last);

		if (e->type == ASP_DIR) {
			struct asp_dir *cd;
			if (asp_diropen_at(asp_dirfd(d), e->name, &cd) == 0) {
				c->r->newline(c->rctx);
				walk_dir(c, cd, depth + 1);
				asp_dirclose(cd);
			} else {
				c->r->error(c->rctx, "error opening dir");
				c->r->newline(c->rctx);
				(*c->errors)++;
			}
		} else {
			c->r->newline(c->rctx);
		}

		c->path.len = pathlen;
		c->path.data[pathlen] = '\0';
	}

	free(ev.v);
	arena_rewind(&c->arena, mk);
}

void asp_walk(const char *root, const struct walk_opts *opts,
	      const struct renderer *r, void *ctx, struct totals *tot, int *errors)
{
	struct asp_dir *d;
	if (asp_diropen(root, &d) != 0) {
		r->root(ctx, root, 1); /* failed: renderer prints the error marker */
		(*errors)++;
		return;
	}
	r->root(ctx, root, 0);
	tot->dirs++; /* root counts as a directory */

	struct wctx c;
	arena_init(&c.arena, 0);
	dstr_init(&c.path);
	dstr_appendz(&c.path, root);
	while (c.path.len > 1 && c.path.data[c.path.len - 1] == '/') {
		c.path.len--;
		c.path.data[c.path.len] = '\0';
	}
	c.opts = opts;
	c.r = r;
	c.rctx = ctx;
	c.tot = tot;
	c.errors = errors;

	walk_dir(&c, d, 1);

	asp_dirclose(d);
	dstr_free(&c.path);
	arena_destroy(&c.arena);
}
