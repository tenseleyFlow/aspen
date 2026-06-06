#include "traverse.h"
#include "arena.h"
#include "dstr.h"
#include "entry.h"
#include "util.h"
#include "sys/dir.h"
#include "sys/xstat.h"

#include <stdlib.h>
#include <string.h>

struct wctx {
	struct arena arena;
	struct dstr path; /* current path, no trailing slash */
	const struct walk_opts *opts;
	asp_visit_fn visit;
	void *vctx;
	struct totals tot;
	int errors;
};

/* A directory's entry pointers, gathered before sorting/emitting. Pointers are
 * cheap (8B); the entries themselves live in the arena. */
struct evec {
	struct entry **v;
	size_t n, cap;
};

static void evec_push(struct evec *ev, struct entry *e)
{
	if (ev->n == ev->cap) {
		ev->cap = ev->cap ? ev->cap * 2 : 32;
		ev->v = asp_xrealloc(ev->v, ev->cap * sizeof *ev->v);
	}
	ev->v[ev->n++] = e;
}

/* Do we need a stat, or does d_type already answer the question? */
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

static void walk_dir(struct wctx *c, struct asp_dir *d, int depth)
{
	struct arena_marker mk = arena_mark(&c->arena);
	size_t pathlen = c->path.len;
	struct evec ev = { NULL, 0, 0 };
	struct asp_dirent de;
	int r;

	while ((r = asp_dirread(d, &de)) == 1) {
		if (!c->opts->all && de.name[0] == '.')
			continue; /* dotfile policy (no -a) */

		enum asp_type t = de.type;
		if (needs_stat(t, c->opts)) {
			struct asp_statinfo si;
			if (asp_stat_at(asp_dirfd(d), de.name, 0, &si) == 0) {
				if (t == ASP_UNKNOWN)
					t = asp_type_from_mode(si.mode);
			} else {
				/* tree drops entries whose stat fails (a racy
				 * behavior on static trees this never fires). */
				c->errors++;
				continue;
			}
		}

		struct entry *e = entry_new(&c->arena, de.name, strlen(de.name), t);
		evec_push(&ev, e);
	}
	if (r < 0)
		c->errors++;

	/* Sorting lands in Sprint 05; readdir order is fine for set-equality. */

	for (size_t i = 0; i < ev.n; i++) {
		struct entry *e = ev.v[i];
		int is_last = (i + 1 == ev.n);

		dstr_appendc(&c->path, '/');
		dstr_append(&c->path, e->name, e->namelen);

		if (e->type == ASP_DIR)
			c->tot.dirs++;
		else
			c->tot.files++;

		if (c->visit)
			c->visit(c->vctx, e, c->path.data, depth, is_last);

		if (e->type == ASP_DIR) {
			struct asp_dir *cd;
			if (asp_diropen_at(asp_dirfd(d), e->name, &cd) == 0) {
				walk_dir(c, cd, depth + 1);
				asp_dirclose(cd);
			} else {
				c->errors++; /* Sprint 02 renders [error opening dir] */
			}
		}

		c->path.len = pathlen;
		c->path.data[pathlen] = '\0';
	}

	free(ev.v);
	arena_rewind(&c->arena, mk);
}

int asp_walk(const char *root, const struct walk_opts *opts,
	     asp_visit_fn visit, void *ctx, struct totals *tot)
{
	struct wctx c;
	arena_init(&c.arena, 0);
	dstr_init(&c.path);
	dstr_appendz(&c.path, root);
	/* normalize: drop trailing '/' (except a lone "/") so joins don't double up */
	while (c.path.len > 1 && c.path.data[c.path.len - 1] == '/') {
		c.path.len--;
		c.path.data[c.path.len] = '\0';
	}

	c.opts = opts;
	c.visit = visit;
	c.vctx = ctx;
	c.tot.dirs = 0;
	c.tot.files = 0;
	c.errors = 0;

	struct asp_dir *d;
	if (asp_diropen(root, &d) != 0) {
		c.errors++;
	} else {
		c.tot.dirs = 1; /* root counts as a directory, like tree */
		walk_dir(&c, d, 1);
		asp_dirclose(d);
	}

	if (tot)
		*tot = c.tot;
	dstr_free(&c.path);
	arena_destroy(&c.arena);
	return c.errors;
}
