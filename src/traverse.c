#include "traverse.h"
#include "render.h"
#include "arena.h"
#include "dstr.h"
#include "entry.h"
#include "glob.h"
#include "hashtab.h"
#include "options.h"
#include "sort.h"
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

/* Sorting by size/time needs the stat data too. */
static int sort_needs_stat(const struct options *o)
{
	return o->sort == SORT_SIZE || o->sort == SORT_MTIME || o->sort == SORT_CTIME;
}

/* patterns come from argv (writable), which patmatch needs for its '|' split. */
static int pat_match_any(const char **pats, size_t n, const char *name, int isdir, int ic)
{
	/* tree treats patmatch's result as truthy: a match (1) AND a syntax error
	 * (-1) both count, so a malformed pattern matches everything. */
	for (size_t i = 0; i < n; i++)
		if (asp_patmatch(name, (char *)pats[i], isdir, ic) != 0)
			return 1;
	return 0;
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

/* Modes that need the whole tree built before emitting. (--fromfile: Sprint 10) */
static int needfulltree(const struct options *o)
{
	return o->duflag || o->prune || o->matchdirs;
}

/* Read one directory's entries into ev (entries from the arena). suppress_pat
 * disables -P for this level (--matchdirs on a name-matched directory). */
static void read_level(struct wctx *c, struct asp_dir *d, struct evec *ev, int suppress_pat)
{
	const struct options *o = c->o;
	int want_st = meta_wanted(o) || sort_needs_stat(o) || o->colorize;
	int dirfd = asp_dirfd(d);
	struct asp_dirent de;
	int r;

	while ((r = asp_dirread(d, &de)) == 1) {
		if (!o->all && de.name[0] == '.')
			continue;

		enum asp_type t = de.type;
		struct asp_statinfo si;
		int have_si = 0;

		/* lstat when: type unknown, metadata/sort columns need it, or a
		 * non-link flag needs it. Links are stat-followed separately below. */
		if (t == ASP_UNKNOWN || want_st || (t != ASP_LNK && nonlink_needs_stat(t, o))) {
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
			if (want_st) /* link columns/sort use the link's own lstat (tree) */
				e->st = arena_memdup(&c->arena, &si, sizeof si);
		}

		if (t == ASP_LNK) {
			fill_link(c, dirfd, e);
			struct asp_statinfo ts;
			if (asp_stat_at(dirfd, de.name, 1, &ts) == 0) {
				e->ltype = (uint16_t)asp_type_from_mode(ts.mode);
				e->lmode = ts.mode;
				if (e->ltype == ASP_REG && is_exec(ts.mode))
					e->flags |= ENT_LEXEC;
				e->ino = ts.ino;
				e->dev = ts.dev;
			} else {
				e->flags |= ENT_ORPHAN;
			}
		}

		int isdir = (t == ASP_DIR) || (t == ASP_LNK && e->ltype == ASP_DIR);

		/* -P applies to non-dirs only (dirs always pass so we can descend),
		 * unless -l makes a symlink dir-like; suppressed by --matchdirs. -I
		 * applies to everything. Names match by basename (tree). */
		if (!suppress_pat && o->npat &&
		    t != ASP_DIR && !(o->follow && t == ASP_LNK && e->ltype == ASP_DIR) &&
		    !pat_match_any(o->patterns, o->npat, de.name, isdir, o->ignorecase))
			continue;
		if (o->nipat &&
		    pat_match_any(o->ipatterns, o->nipat, de.name, isdir, o->ignorecase))
			continue;
		if (o->dirsonly && !isdir)
			continue;

		evec_push(ev, e);
	}
	if (r < 0)
		(*c->errors)++;
}

static void walk_dir(struct wctx *c, struct asp_dir *d, int depth)
{
	struct arena_marker mk = arena_mark(&c->arena);
	size_t pathlen = c->path.len;
	struct evec ev = { NULL, 0, 0 };
	const struct options *o = c->o;
	int dirfd = asp_dirfd(d);

	read_level(c, d, &ev, 0);

	asp_sort(ev.v, ev.n, o);

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

/* Build the full subtree of an open directory into arena entries (->child set
 * for descended dirs). du aggregation happens later (post-prune). */
static struct entry **build_level(struct wctx *c, struct asp_dir *d, int depth,
				  int suppress_pat)
{
	const struct options *o = c->o;
	struct evec ev = { NULL, 0, 0 };
	int dirfd = asp_dirfd(d);

	read_level(c, d, &ev, suppress_pat);

	for (size_t i = 0; i < ev.n; i++) {
		struct entry *e = ev.v[i];
		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);

		/* --matchdirs: a dir whose name matches -P shows its contents
		 * unfiltered and is protected from --prune. */
		int child_suppress = suppress_pat;
		if (o->matchdirs && o->npat && dir_like &&
		    pat_match_any(o->patterns, o->npat, e->name, 1, o->ignorecase)) {
			e->flags |= ENT_MATCHED;
			child_suppress = 1;
		}

		int descend = (e->type == ASP_DIR) ||
			(o->follow && e->type == ASP_LNK && e->ltype == ASP_DIR);
		const char *post_err = NULL;
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
				e->child = build_level(c, cd, depth + 1, child_suppress);
				asp_dirclose(cd);
			} else {
				e->err = "error opening dir";
				(*c->errors)++;
			}
		} else if (post_err) {
			e->err = post_err;
		}
	}

	struct entry **arr = arena_alloc(&c->arena, (ev.n + 1) * sizeof *arr);
	for (size_t i = 0; i < ev.n; i++)
		arr[i] = ev.v[i];
	arr[ev.n] = NULL;
	free(ev.v);
	return arr;
}

/* Bottom-up --prune of empty directories (not --matchdirs-protected). */
static void prune_level(struct entry **arr)
{
	size_t n = 0, w = 0;
	while (arr[n])
		n++;
	for (size_t i = 0; i < n; i++) {
		struct entry *e = arr[i];
		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like && e->child)
			prune_level(e->child);
		int empty = dir_like && (!e->child || e->child[0] == NULL);
		if (empty && !(e->flags & ENT_MATCHED))
			continue; /* drop empty dir */
		arr[w++] = e;
	}
	arr[w] = NULL;
}

/* --du: bottom-up size aggregation over the (post-prune) tree. Each directory's
 * displayed size becomes its own inode size plus the sum of its contents, like
 * tree (which accumulates into the dir's st_size). Returns this level's total. */
static off_t du_aggregate(struct entry **arr)
{
	off_t sum = 0;
	for (size_t i = 0; arr[i]; i++) {
		struct entry *e = arr[i];
		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like && e->child && e->st)
			((struct asp_statinfo *)e->st)->size += du_aggregate(e->child);
		if (e->st)
			sum += e->st->size;
	}
	return sum;
}

/* Emit a pre-built level (full-tree mode). */
static void emit_level(struct wctx *c, struct entry **arr, int depth)
{
	const struct options *o = c->o;
	size_t pathlen = c->path.len;
	size_t n = 0;
	while (arr[n])
		n++;
	asp_sort(arr, n, o);

	for (size_t i = 0; i < n; i++) {
		struct entry *e = arr[i];
		int is_last = (i + 1 == n);

		dstr_appendc(&c->path, '/');
		dstr_append(&c->path, e->name, e->namelen);

		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like)
			c->tot->dirs++;
		else
			c->tot->files++;

		c->r->entry(c->rctx, e, c->path.data, depth, is_last);

		if (e->child) {
			c->r->newline(c->rctx);
			emit_level(c, e->child, depth + 1);
		} else {
			if (e->err)
				c->r->error(c->rctx, e->err);
			c->r->newline(c->rctx);
		}

		c->path.len = pathlen;
		c->path.data[pathlen] = '\0';
	}
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

	/* Root stat: for -x device, -l cycle seed, the metadata bracket, and color. */
	struct asp_statinfo rs;
	const struct asp_statinfo *root_st = NULL;
	if (meta_wanted(o) || o->xdev || o->follow || o->colorize) {
		if (asp_stat_at(asp_dirfd(d), ".", 1, &rs) == 0) {
			c.root_dev = rs.dev;
			if (o->follow)
				inoset_add(&c.seen, rs.ino, rs.dev);
			root_st = &rs;
		}
	}

	if (needfulltree(o)) {
		struct entry **top = build_level(&c, d, 1, 0);
		if (o->prune)
			prune_level(top);
		if (o->duflag) {
			off_t dusum = du_aggregate(top);
			if (root_st) { /* root total = root's own size + contents */
				rs.size += dusum;
				tot->size = rs.size;
			} else {
				tot->size = dusum;
			}
		}
		r->root(ctx, root, 0, root_st);
		tot->dirs++;
		emit_level(&c, top, 1);
	} else {
		r->root(ctx, root, 0, root_st);
		tot->dirs++;
		walk_dir(&c, d, 1);
	}

	asp_dirclose(d);
	inoset_destroy(&c.seen);
	dstr_free(&c.path);
	arena_destroy(&c.arena);
}
