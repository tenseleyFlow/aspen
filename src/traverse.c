#include "traverse.h"
#include "render.h"
#include "arena.h"
#include "dstr.h"
#include "entry.h"
#include "hashtab.h"
#include "options.h"
#include "util.h"
#include "sys/dir.h"
#include "sys/xstat.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct wctx {
	struct arena arena;
	struct dstr path;
	const struct options *o;
	const struct renderer *r;
	void *rctx;
	struct totals *tot;
	int *errors;
	struct inoset seen; /* -l cycle detection */
	dev_t root_dev;
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

static int is_exec(mode_t m)
{
	return (m & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

/* Non-symlink entries stat only when a flag needs the metadata. (Symlinks are
 * always stat-followed below, because tree counts/classifies them by target.) */
static int nonlink_needs_stat(enum asp_type t, const struct options *o)
{
	if (o->classify && t == ASP_REG) /* exec bit for '*' */
		return 1;
	if (o->xdev && t == ASP_DIR) /* device id for -x descent */
		return 1;
	if (o->follow && t == ASP_DIR) /* inode/dev for -l cycle set */
		return 1;
	return 0;
}

/* Any flag that needs the bracketed metadata column -> stat every entry. */
static int meta_wanted(const struct options *o)
{
	return o->sizeflag || o->permflag || o->userflag || o->groupflag ||
	       o->dateflag || o->inodeflag || o->devflag || o->duflag;
}

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
	const struct options *o = c->o;
	int meta = meta_wanted(o);
	int r;
	int dirfd = asp_dirfd(d);

	while ((r = asp_dirread(d, &de)) == 1) {
		if (!o->all && de.name[0] == '.')
			continue;

		enum asp_type t = de.type;
		struct asp_statinfo si;
		int have_si = 0;

		/* lstat when: type unknown, metadata columns requested, or a
		 * non-link flag needs it. Links are stat-followed separately below. */
		if (t == ASP_UNKNOWN || meta || (t != ASP_LNK && nonlink_needs_stat(t, o))) {
			if (asp_stat_at(dirfd, de.name, 0, &si) == 0) {
				have_si = 1;
				if (t == ASP_UNKNOWN)
					t = asp_type_from_mode(si.mode);
			} else {
				(*c->errors)++;
				continue; /* tree drops entries whose stat fails */
			}
		}

		struct entry *e = entry_new(&c->arena, de.name, strlen(de.name), t);
		if (have_si) {
			e->flags |= ENT_STATTED;
			e->ino = si.ino;
			e->dev = si.dev;
			if (t == ASP_REG && is_exec(si.mode))
				e->flags |= ENT_EXEC;
			if (meta) /* link columns show the link's own lstat (tree) */
				e->st = arena_memdup(&c->arena, &si, sizeof si);
		}

		if (t == ASP_LNK) {
			fill_link(c, dirfd, e);
			/* Always stat-follow: tree's getinfo always does, so a symlink
			 * to a directory is counted/classified as a directory in every
			 * mode. Costs one stat per symlink (tree pays it too); regular
			 * files and dirs stay stat-free. */
			struct asp_statinfo ts;
			if (asp_stat_at(dirfd, de.name, 1, &ts) == 0) {
				e->ltype = (uint16_t)asp_type_from_mode(ts.mode);
				if (e->ltype == ASP_REG && is_exec(ts.mode))
					e->flags |= ENT_LEXEC;
				e->ino = ts.ino; /* target identity for -l cycle */
				e->dev = ts.dev;
			} else {
				e->flags |= ENT_ORPHAN;
			}
		}

		/* -d keeps only directory-like entries (incl. symlink-to-dir, like tree). */
		if (o->dirsonly &&
		    !(t == ASP_DIR || (t == ASP_LNK && e->ltype == ASP_DIR)))
			continue;

		evec_push(&ev, e);
	}
	if (r < 0)
		(*c->errors)++;

	if (o->sort != SORT_NONE)
		qsort(ev.v, ev.n, sizeof *ev.v, cmp_name);

	for (size_t i = 0; i < ev.n; i++) {
		struct entry *e = ev.v[i];
		int is_last = (i + 1 == ev.n);

		dstr_appendc(&c->path, '/');
		dstr_append(&c->path, e->name, e->namelen);

		/* A symlink to a directory counts as a directory (tree, all modes). */
		int dir_like = e->type == ASP_DIR ||
			(e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like)
			c->tot->dirs++;
		else
			c->tot->files++;

		c->r->entry(c->rctx, e, c->path.data, depth, is_last);

		/* descent decision */
		int descend = 0;
		const char *post_err = NULL;
		if (e->type == ASP_DIR)
			descend = 1;
		else if (o->follow && e->type == ASP_LNK && e->ltype == ASP_DIR)
			descend = 1;

		if (descend && o->level >= 0 && depth >= o->level)
			descend = 0;
		if (descend && o->xdev && e->type == ASP_DIR && e->dev != c->root_dev)
			descend = 0;
		if (descend && o->follow) {
			if (inoset_has(&c->seen, e->ino, e->dev)) {
				post_err = "recursive, not followed";
				descend = 0;
			} else {
				inoset_add(&c->seen, e->ino, e->dev);
			}
		}

		if (descend) {
			struct asp_dir *cd;
			if (asp_diropen_at(dirfd, e->name, &cd) == 0) {
				c->r->newline(c->rctx);
				walk_dir(c, cd, depth + 1);
				asp_dirclose(cd);
			} else {
				c->r->error(c->rctx, "error opening dir");
				c->r->newline(c->rctx);
				(*c->errors)++;
			}
		} else {
			if (post_err)
				c->r->error(c->rctx, post_err);
			c->r->newline(c->rctx);
		}

		c->path.len = pathlen;
		c->path.data[pathlen] = '\0';
	}

	free(ev.v);
	arena_rewind(&c->arena, mk);
}

void asp_walk(const char *root, const struct options *o,
	      const struct renderer *r, void *ctx, struct totals *tot, int *errors)
{
	struct asp_dir *d;
	if (asp_diropen(root, &d) != 0) {
		r->root(ctx, root, 1, NULL);
		(*errors)++;
		return;
	}

	struct wctx c;
	arena_init(&c.arena, 0);
	dstr_init(&c.path);
	dstr_appendz(&c.path, root);
	while (c.path.len > 1 && c.path.data[c.path.len - 1] == '/') {
		c.path.len--;
		c.path.data[c.path.len] = '\0';
	}
	c.o = o;
	c.r = r;
	c.rctx = ctx;
	c.tot = tot;
	c.errors = errors;
	c.root_dev = 0;
	inoset_init(&c.seen);

	/* Root stat: for -x device, -l cycle seed, and the metadata bracket. */
	struct asp_statinfo rs;
	const struct asp_statinfo *root_st = NULL;
	if (meta_wanted(o) || o->xdev || o->follow) {
		if (asp_stat_at(asp_dirfd(d), ".", 1, &rs) == 0) {
			c.root_dev = rs.dev;
			if (o->follow)
				inoset_add(&c.seen, rs.ino, rs.dev);
			if (meta_wanted(o))
				root_st = &rs;
		}
	}

	r->root(ctx, root, 0, root_st);
	tot->dirs++;

	walk_dir(&c, d, 1);

	asp_dirclose(d);
	inoset_destroy(&c.seen);
	dstr_free(&c.path);
	arena_destroy(&c.arena);
}
