#include "traverse.h"
#include "render.h"
#include "arena.h"
#include "dstr.h"
#include "entry.h"
#include "filter.h"
#include "fromfile.h"
#include "glob.h"
#include "hashtab.h"
#include "info.h"
#include "iouring.h"
#include "options.h"
#include "pool.h"
#include "sort.h"
#include "util.h"
#include "sys/dir.h"
#include "sys/xstat.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef ASP_INFO_PATH
#define ASP_INFO_PATH "/usr/share/finfo/global_info" /* tree's default --info file */
#endif

struct evec {
	struct entry **v;
	size_t n, cap;
};

struct wctx {
	struct arena arena;
	struct dstr path;
	const struct options *o;
	const struct renderer *r;
	void *rctx;
	struct totals *tot;
	int *errors;
	struct inoset seen; /* -l cycle detection */
	struct ignorefile *fstack; /* --gitignore filter stack */
	struct infofile *istack;   /* --info annotation stack */
	int info_top;              /* current dir has its own .info */
	dev_t root_dev;
	const char *root_err;      /* SR-2.12: set if the root itself tripped --filelimit */
	struct statprov *sp;       /* metadata-stat backend seam (serial/pool/uring) */
	struct asp_pool *prefetch; /* SR-3.5: opt-in cross-dir read-prefetch pool, or NULL */

	/* Reusable scratch — kept across the whole walk so a level pays no per-dir
	 * malloc (the "arena, no malloc churn" budget). One entry-vector per active
	 * depth (a depth's vector is live for exactly one dir at a time, since
	 * siblings are sequential and recursion uses depth+1); one deferred-stat
	 * vector (consumed within read_level, which never recurses). */
	struct evec *epool;
	size_t epoolcap;
	struct evec defer;
};

/* Threshold below which a level's deferred stats run inline — small levels
 * aren't worth the pool hand-off (~a few us vs ~1us per stat). */
/* Minimum deferred-stat batch to spawn/use the worker pool. Set ABOVE the spawn
 * break-even, not at the point parallelism becomes merely possible: the 15-thread
 * spawn (~45 thr_new + sigaction/umtx setup) only amortizes past a few hundred
 * stats, so a lower value made the DEFAULT config lose to tree on the most common
 * `-s ~/project` workload (one dir of 64-250 files) — a directive-#2 violation
 * (audit A3). Below this, the walk stays on aspen's serial path, which already
 * beats tree. Conservatively high so it never loses on the slow FreeBSD-compat
 * dev box; faster platforms leave a little mid-size parallelism unused but still
 * win serially. */
#define ASP_STAT_PAR_MIN 384

/* Push the current directory's .gitignore (c->path must be the dir path). */
static struct ignorefile *push_dir_gitignore(struct wctx *c)
{
	if (!c->o->gitignore)
		return NULL;
	struct ignorefile *ig = gitignore_load_dir(c->path.data);
	gitstack_push(&c->fstack, ig);
	return ig;
}

/* Push the current directory's .info; record whether this dir has one. */
static struct infofile *push_dir_info(struct wctx *c)
{
	if (!c->o->showinfo) {
		c->info_top = 0;
		return NULL;
	}
	struct infofile *inf = info_load_dir(c->path.data);
	infostack_push(&c->istack, inf);
	c->info_top = (inf != NULL);
	return inf;
}

static void evec_push(struct evec *ev, struct entry *e)
{
	if (ev->n == ev->cap) {
		ev->cap = ev->cap ? ev->cap * 2 : 32;
		ev->v = asp_xrealloc(ev->v, ev->cap * sizeof *ev->v);
	}
	ev->v[ev->n++] = e;
}

/* A cleared, reusable entry vector for `depth` from the wctx pool (grows the pool
 * as the walk descends; the buffer is retained across siblings). */
static struct evec *evec_at(struct wctx *c, int depth)
{
	if ((size_t)depth >= c->epoolcap) {
		size_t nc = c->epoolcap ? c->epoolcap * 2 : 16;
		while ((size_t)depth >= nc)
			nc *= 2;
		c->epool = asp_xrealloc(c->epool, nc * sizeof *c->epool);
		for (size_t i = c->epoolcap; i < nc; i++)
			c->epool[i] = (struct evec){ NULL, 0, 0 };
		c->epoolcap = nc;
	}
	c->epool[depth].n = 0; /* reuse the buffer, reset the count */
	return &c->epool[depth];
}

/* Zero the reusable scratch (call once per wctx, before any evec_at/read_level). */
static void wctx_scratch_init(struct wctx *c)
{
	c->epool = NULL;
	c->epoolcap = 0;
	c->defer = (struct evec){ NULL, 0, 0 };
	c->root_err = NULL;
}

static void wctx_scratch_free(struct wctx *c)
{
	for (size_t i = 0; i < c->epoolcap; i++)
		free(c->epool[i].v);
	free(c->epool);
	free(c->defer.v);
}

static int is_exec(mode_t m)
{
	return (m & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

/* Deferred fd-limit bump (SR-3.9). The fd-relative walk holds one dir fd per
 * active depth, so a very deep tree can exhaust the soft RLIMIT_NOFILE. Instead
 * of bumping at startup (a syscall pair on every run), raise the soft limit to
 * the hard limit lazily — only when a descent actually fails with EMFILE/ENFILE
 * — then retry the open once. Trivial and shallow runs pay nothing. One-shot;
 * the main thread is the only opener (the pool only stats). */
static int g_fdlimit_raised;

static int diropen_at_deep(int dirfd, const char *name, struct asp_dir **cd)
{
	int r = asp_diropen_at(dirfd, name, cd);
	if (r == 0 || g_fdlimit_raised || (errno != EMFILE && errno != ENFILE))
		return r;
	g_fdlimit_raised = 1;
	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max) {
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &rl) == 0)
			r = asp_diropen_at(dirfd, name, cd); /* retry with headroom */
	}
	return r;
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

/* The stat-need invariant, centralized (SR02-2.2 / CLAUDE.md design invariant).
 * Three distinct questions, each named so a call site can't drift its own tail:
 *
 *   want_entry_stat — does THIS entry need its full metadata stat (e->st filled)?
 *     Metadata column, size/time sort, or colorize. Type-only needs (the -F exec
 *     bit, -x device id, -l cycle ino/dev) are decided per entry by
 *     nonlink_needs_stat, NOT here. (read_level, synth_level — note --du is part of
 *     meta_wanted, so it's covered without a separate term.)
 *   any_stat_path — will ANY entry be statted on this walk? = want_entry_stat OR a
 *     type-only need (classify/xdev/follow make nonlink_needs_stat fire). Gates
 *     building the stat backend in asp_statprov_create: no stats -> no pool/ring.
 *   root_needs_stat — does the ROOT argument itself need a stat? Its line shows
 *     metadata, color and the -F type suffix; it is never sorted and its descent
 *     flags don't affect its own stat — meta/color/classify only.
 */
static int want_entry_stat(const struct options *o)
{
	return meta_wanted(o) || sort_needs_stat(o) || o->colorize;
}
static int any_stat_path(const struct options *o)
{
	return want_entry_stat(o) || o->classify || o->xdev || o->follow;
}
static int root_needs_stat(const struct options *o)
{
	return meta_wanted(o) || o->colorize || o->classify;
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

/* tree 2.3.2 -P/-I matching (file.c:132, `patinclude(name,false) ||
 * patinclude(fpath,true)`): a pattern matches an entry if it matches the BASENAME,
 * the FULL PATH from the walk root, OR any '/'-suffix of that path. (aspen was
 * first ported from tree 2.2.1, which matched the basename only — so any pattern
 * containing '/' silently matched nothing; audit A4.) `c->path` is the entry's
 * PARENT directory here; append the name to form the full path, match, restore. */
static int pat_match_full(struct wctx *c, const char **pats, size_t n,
			  const char *name, size_t namelen, int isdir)
{
	int ic = c->o->ignorecase;
	size_t save = c->path.len;
	dstr_appendc(&c->path, '/');
	dstr_append(&c->path, name, namelen);
	const char *fpath = c->path.data;
	int matched = 0;
	for (size_t i = 0; i < n && !matched; i++) {
		char *pat = (char *)pats[i];
		if (asp_patmatch(name, pat, isdir, ic) != 0 ||
		    asp_patmatch(fpath, pat, isdir, ic) != 0) {
			matched = 1;
			break;
		}
		for (const char *pc = strchr(fpath, '/'); pc && *pc; pc = strchr(pc + 1, '/'))
			if (asp_patmatch(pc + 1, pat, isdir, ic) != 0) {
				matched = 1;
				break;
			}
	}
	c->path.len = save;
	c->path.data[save] = '\0';
	return matched;
}

static void fill_link(struct wctx *c, int dirfd, struct entry *e)
{
	char buf[PATH_MAX]; /* a symlink target is bounded by PATH_MAX */
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
	return o->duflag || o->prune || o->matchdirs || o->filelimit > 0 || o->condense;
}

/* Read one directory's entries into ev (entries from the arena). suppress_pat
 * disables -P for this level (--matchdirs on a name-matched directory). */
struct stat_job {
	int dirfd;
	int want_st;
	struct entry **ents;
};

/* One deferred metadata stat. Runs on the pool: touches only entry `i`'s own
 * fields (and its pre-allocated e->st), never the arena, so it is race-free. */
static void stat_one(void *arg, size_t i)
{
	struct stat_job *j = arg;
	struct entry *e = j->ents[i];
	struct asp_statinfo si;
	if (asp_stat_at(j->dirfd, e->name, 0, &si) != 0) {
		e->flags |= ENT_STAT_FAILED; /* dropped in order by the caller */
		return;
	}
	e->flags |= ENT_STATTED;
	e->ino = si.ino;
	e->dev = si.dev;
	if (e->type == ASP_REG && is_exec(si.mode))
		e->flags |= ENT_EXEC;
	if (j->want_st)
		memcpy((void *)e->st, &si, sizeof si); /* e->st pre-allocated by caller */
}

/* The metadata-stat backend seam. read_level is backend-blind: it hands a batch
 * of deferred entries to statprov_batch, which stats them (into e->st/ino/dev,
 * ENT_STAT_FAILED on per-entry failure) using whichever backend was selected at
 * startup — serial, the thread pool, or io_uring. Built once (in render_tree)
 * and shared across roots. ASP_IO selects (default/threads=pool, uring=io_uring,
 * serial=inline); only created when some flag actually stats every entry. */
struct statprov {
	struct asp_pool *pool; /* NULL = serial, or pool not spawned yet (lazy) */
	struct asp_ring *ring; /* NULL = no io_uring */
	int want_pool;         /* a pool is wanted but deferred to the first big batch */
	int workers;           /* resolved worker count for the lazy pool */
};

struct statprov *asp_statprov_create(const struct options *o)
{
	struct statprov *sp = asp_xmalloc(sizeof *sp);
	sp->pool = NULL;
	sp->ring = NULL;
	sp->want_pool = 0;
	sp->workers = 0;

	int stat_heavy = any_stat_path(o);
	const char *iomode = getenv("ASP_IO");
	/* --threads 1 (and ASP_IO=serial) force the inline path: the block below is
	 * skipped, so no pool/ring is ever constructed for a serial run. */
	int serial = (o->threads == 1) || (iomode && !strcmp(iomode, "serial"));
	if (stat_heavy && !serial && !(o->fromfile || o->fromtabfile)) {
		if (iomode && !strcmp(iomode, "uring")) {
			sp->ring = asp_ring_create(256); /* NULL if unavailable */
			if (!sp->ring) /* perf-truth: don't silently pretend uring ran */
				fprintf(stderr, "aspen: ASP_IO=uring requested but io_uring "
						"is unavailable; using the thread pool.\n");
		}
		if (!sp->ring) {
			int workers = (o->threads > 0) ? o->threads
						       : asp_pool_default_workers();
			if (workers > 1) {
				/* SR02-1.1: defer the actual pool spawn (threads +,
				 * on FreeBSD, the libthr dlopen) to the first batch that
				 * is big enough to parallelize. Low-fanout trees (the
				 * common `-s ~/project`) never reach the threshold and
				 * stay fully serial — no spawn, no teardown, no variance. */
				sp->want_pool = 1;
				sp->workers = workers;
			}
		}
	}
	return sp;
}

void asp_statprov_destroy(struct statprov *sp)
{
	if (!sp)
		return;
	asp_pool_destroy(sp->pool);
	asp_ring_destroy(sp->ring);
	free(sp);
}

/* Stat the n deferred entries (dirfd-relative) into their fields; the chosen
 * backend is internal. Sub-threshold batches run inline regardless. */
static void statprov_batch(struct statprov *sp, int dirfd, struct entry **ents,
			   size_t n, int want_st)
{
	/* io_uring (opt-in) batches statx; on any driver error reset the per-entry
	 * failure marks and fall back to the pool/inline path. */
	if (sp->ring && n >= ASP_STAT_PAR_MIN) {
		if (asp_ring_stat_batch(sp->ring, dirfd, ents, n, want_st) == 0)
			return;
		for (size_t k = 0; k < n; k++)
			ents[k]->flags &= (uint16_t)~ENT_STAT_FAILED;
	}
	/* SR02-1.1: spawn the deferred pool on the first batch that crosses the
	 * threshold (once — if creation fails we stay serial, no repeated attempts). */
	if (sp->want_pool && n >= ASP_STAT_PAR_MIN) {
		sp->want_pool = 0;
		sp->pool = asp_pool_create(sp->workers);
	}
	struct stat_job j = { dirfd, want_st, ents };
	struct asp_pool *p = (sp->pool && n >= ASP_STAT_PAR_MIN) ? sp->pool : NULL;
	asp_pool_for(p, n, stat_one, &j);
}

static void read_level(struct wctx *c, struct asp_dir *d, struct evec *ev, int suppress_pat)
{
	const struct options *o = c->o;
	int want_st = want_entry_stat(o);
	int dirfd = asp_dirfd(d);
	struct asp_dirent de;
	int r;
	/* Non-symlink, known-type entries whose only stat need is metadata/sort/
	 * color/-F/-x/-l are statted in a deferred batch (parallelizable) — the
	 * dominant cost of -s/-p/-D/--du. Filtering uses name + d_type only, so
	 * deferring past the filters is correct (and skips stats on filtered-out
	 * entries). Symlinks and DT_UNKNOWN still stat inline (filtering needs it). */
	struct evec *defer = &c->defer; /* reusable; read_level never recurses */
	defer->n = 0;

	while ((r = asp_dirread(d, &de)) == 1) {
		/* -H: tree's read_dir unconditionally hides any "00Tree.html" entry in
		 * HTML mode (tree.c:898) so the -R generated files never list themselves
		 * (and a pre-existing one stays hidden). Applies even without -R. */
		if (o->format == OUT_HTML && de.name[0] == '0' &&
		    !strcmp(de.name, "00Tree.html"))
			continue;
		if (!o->all && de.name[0] == '.')
			continue;

		enum asp_type t = de.type;
		struct asp_statinfo si;
		int have_si = 0;
		int deferred = 0;

		/* Resolve an unknown d_type now (filtering/display need the type). */
		if (t == ASP_UNKNOWN) {
			if (asp_stat_at(dirfd, de.name, 0, &si) == 0) {
				have_si = 1;
				t = asp_type_from_mode(si.mode);
			} else {
				(*c->errors)++;
				continue; /* tree drops entries whose stat fails */
			}
		}

		struct entry *e = entry_new(&c->arena, de.name, de.namelen, t);
		if (have_si) {
			e->flags |= ENT_STATTED;
			e->ino = si.ino;
			e->dev = si.dev;
			if (t == ASP_REG && is_exec(si.mode))
				e->flags |= ENT_EXEC;
			if (want_st)
				e->st = arena_memdup(&c->arena, &si, sizeof si);
		} else if (t != ASP_LNK && (want_st || nonlink_needs_stat(t, o))) {
			/* defer this stat to the batch pass below */
			deferred = 1;
			if (want_st) /* pre-allocate so workers never touch the arena */
				e->st = arena_alloc(&c->arena, sizeof(struct asp_statinfo));
		}

		if (t == ASP_LNK) {
			/* The link's own lstat feeds -s/-D columns (tree shows link
			 * metadata); stat inline since symlinks are uncommon. */
			if (want_st) {
				if (asp_stat_at(dirfd, de.name, 0, &si) == 0) {
					e->flags |= ENT_STATTED;
					e->ino = si.ino;
					e->dev = si.dev;
					e->st = arena_memdup(&c->arena, &si, sizeof si);
				} else {
					(*c->errors)++;
					continue;
				}
			}
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

		/* .gitignore filtering (before -P/-I/-d, like tree). Uses the full
		 * path for absolute patterns. */
		if (c->o->gitignore && c->fstack) {
			size_t save = c->path.len;
			dstr_appendc(&c->path, '/');
			dstr_append(&c->path, de.name, de.namelen);
			int filt = gitignore_filtered(c->fstack, c->path.data, de.name,
						       isdir, c->o->ignorecase);
			c->path.len = save;
			c->path.data[save] = '\0';
			if (filt)
				continue;
		}

		/* -P applies to non-dirs only (dirs always pass so we can descend),
		 * unless -l makes a symlink dir-like; suppressed by --matchdirs. -I
		 * applies to everything. Match basename + full path + path suffixes (A4). */
		if (!suppress_pat && o->npat &&
		    t != ASP_DIR && !(o->follow && t == ASP_LNK && e->ltype == ASP_DIR) &&
		    !pat_match_full(c, o->patterns, o->npat, de.name, de.namelen, isdir))
			continue;
		if (o->nipat &&
		    pat_match_full(c, o->ipatterns, o->nipat, de.name, de.namelen, isdir))
			continue;
		if (o->dirsonly && !isdir)
			continue;

		/* --info: attach matching annotation lines (copied into the arena so
		 * they survive into full-tree emit). */
		if (o->showinfo && c->istack) {
			size_t save = c->path.len;
			dstr_appendc(&c->path, '/');
			dstr_append(&c->path, de.name, de.namelen);
			char **desc = info_check(c->istack, c->path.data, de.name,
						 c->info_top, isdir, o->ignorecase);
			c->path.len = save;
			c->path.data[save] = '\0';
			if (desc) {
				size_t n = 0;
				while (desc[n])
					n++;
				char **cp = arena_alloc(&c->arena, (n + 1) * sizeof *cp);
				for (size_t k = 0; k < n; k++)
					cp[k] = arena_strdup(&c->arena, desc[k]);
				cp[n] = NULL;
				e->info = cp;
			}
		}

		evec_push(ev, e);
		if (deferred)
			evec_push(defer, e);
	}
	if (r < 0)
		(*c->errors)++;

	/* Batch the deferred stats — parallel on the pool for big levels, inline
	 * otherwise. Output order is fixed (by name) before this runs, so timing
	 * is the only thing that changes. */
	if (defer->n) {
		statprov_batch(c->sp, dirfd, defer->v, defer->n, want_st);

		/* Drop entries whose stat failed, preserving order (tree drops them). */
		int dropped = 0;
		for (size_t k = 0; k < defer->n; k++)
			if (defer->v[k]->flags & ENT_STAT_FAILED)
				dropped = 1;
		if (dropped) {
			size_t w = 0;
			for (size_t k = 0; k < ev->n; k++) {
				if (ev->v[k]->flags & ENT_STAT_FAILED) {
					(*c->errors)++;
					continue;
				}
				ev->v[w++] = ev->v[k];
			}
			ev->n = w;
		}
	}
}

/* SR-3.5 cross-dir read-prefetch (opt-in, ASP_PREFETCH). A worker opens+drains a
 * subdirectory purely to pull its inode + dir blocks into the kernel cache ahead
 * of the serial DFS, hiding cold I/O latency. It is advisory: read-only syscalls
 * on its own dirfd, no shared aspen state touched, errors ignored — so output
 * stays byte-identical and it is race-free regardless of how it interleaves.
 *
 * COST (SR02-1.7 / audit L6): this DOUBLES the open/getdents on each prefetched
 * subdir — the worker reads it once to warm the cache, then the serial walk reads
 * it again for the authoritative listing. On a WARM cache that is pure overhead
 * with no benefit (consistent with the retracted 2.3x→1.00x claim); it can only
 * help genuinely COLD I/O where the worker's read overlaps with the serial walk's
 * compute. Default-off and experimental for exactly this reason. A future
 * optimization could hand the worker's drained dirents to the serial consumer to
 * avoid the second read; kept simple (re-read) until that's shown to pay off. */
struct prefetch_job {
	int dirfd;
	struct entry **ents;
};

static void prefetch_one(void *arg, size_t i)
{
	struct prefetch_job *j = arg;
	struct entry *e = j->ents[i];
	if (e->type != ASP_DIR) /* only real dirs; symlinks/files aren't descended-read */
		return;
	struct asp_dir *cd;
	if (asp_diropen_at(j->dirfd, e->name, &cd) != 0)
		return; /* advisory: ignore failures, the serial walk reports them */
	struct asp_dirent de;
	while (asp_dirread(cd, &de) == 1)
		; /* drain to warm the directory's data blocks */
	asp_dirclose(cd);
}

/* Resolve whether to descend into `e` and, if not, why (SR02-2.1). Shared by the
 * streaming (walk_dir) and full-tree (build_level) engines so the parity-critical
 * -L/-x/-l descent logic — home of the SR02-0.2 ordering bug — lives in ONE place.
 * Sets *post_err to "recursive, not followed" for a followed symlink whose target
 * was already seen, and *at_boundary when an otherwise-eligible directory is
 * stopped purely by the -L limit (the -R rerun trigger; emit_level detects the
 * same boundary via a NULL child, so the full-tree caller ignores at_boundary). */
static int decide_descent(struct wctx *c, struct entry *e, int depth,
			  const char **post_err, int *at_boundary)
{
	const struct options *o = c->o;
	*post_err = NULL;
	*at_boundary = 0;
	int dir_like = (e->type == ASP_DIR) ||
		       (o->follow && e->type == ASP_LNK && e->ltype == ASP_DIR);
	if (!dir_like)
		return 0;
	int descend = 1;

	/* -l cycle set. tree records EVERY directory's inode (saveino) BEFORE any
	 * descend/xdev/-L check, so a directory halted at the -L boundary (or excluded
	 * by -x) still blocks a later symlink pointing at it (audit A2). Only a SYMLINK
	 * whose target inode was already seen is "recursive, not followed"; a real
	 * directory revisited via a symlink/hardlink is still descended. e->ino/e->dev
	 * is the target's for a followed symlink (read_level). */
	if (o->follow) {
		if (inoset_has(&c->seen, e->ino, e->dev)) {
			if (e->type == ASP_LNK) {
				*post_err = "recursive, not followed";
				descend = 0;
			}
		} else {
			inoset_add(&c->seen, e->ino, e->dev);
		}
	}

	/* xdev-excluded dirs neither descend nor (under -R) rerun — but are still in
	 * the inode set above (tree's saveino precedes its xdev guard). */
	if (o->xdev && e->type == ASP_DIR && e->dev != c->root_dev)
		return 0;

	/* The -L limit: an otherwise-eligible, non-excluded dir stopped here is the -R
	 * boundary (tree list.c:209); tree clears the "recursive" err at the boundary
	 * (list.c:204). */
	if (o->level >= 0 && depth >= o->level) {
		*at_boundary = 1;
		*post_err = NULL;
		descend = 0;
	}
	return descend;
}

static void walk_dir(struct wctx *c, struct asp_dir *d, int depth)
{
	struct arena_marker mk = arena_mark(&c->arena);
	size_t pathlen = c->path.len;
	struct evec *ev = evec_at(c, depth);
	const struct options *o = c->o;
	int dirfd = asp_dirfd(d);
	struct ignorefile *ig = push_dir_gitignore(c);
	struct infofile *inf = push_dir_info(c);

	read_level(c, d, ev, 0);

	asp_sort(ev->v, ev->n, o);

	/* Cache the buffer + count: recursing deeper may evec_at()->realloc the pool
	 * array (moving the struct evec), but never this level's separately-malloc'd
	 * v buffer — so these locals stay valid across the child walk below. */
	struct entry **vec = ev->v;
	size_t n = ev->n;

	/* SR-3.5: before the serial descent, fan out reads of this level's
	 * subdirectories to warm the cache (only when we will actually descend).
	 * Cache-warming only — the loop below still produces the authoritative
	 * ordered output, now hitting warm pages. Opt-in; default off. */
	if (c->prefetch && n > 1 && (o->level < 0 || depth < o->level)) {
		struct prefetch_job pj = { dirfd, vec };
		asp_pool_for(c->prefetch, n, prefetch_one, &pj);
	}

	/* -R: tree's htmldescend is per-listing state (list.c:148) — 0 until the
	 * first descend-eligible dir is stopped by the -L boundary, then sticky at 10
	 * for every following sibling (even plain files inherit "/00Tree.html"). */
	int htmldescend = 0;

	for (size_t i = 0; i < n; i++) {
		struct entry *e = vec[i];
		int is_last = (i + 1 == n);

		dstr_appendc(&c->path, '/');
		dstr_append(&c->path, e->name, e->namelen);

		/* A symlink to a directory counts as a directory (tree, all modes). */
		int dir_like = e->type == ASP_DIR ||
			(e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like)
			c->tot->dirs++;
		else
			c->tot->files++;

		/* Descent decision is computed BEFORE the entry is emitted (tree calls
		 * printfile with the resolved descend+htmldescend), so the renderer knows
		 * whether this is a normal dir, a -R boundary, or a plain entry. */
		const char *post_err;
		int at_boundary;
		int descend = decide_descent(c, e, depth, &post_err, &at_boundary);

		/* -R: an eligible dir stopped by the -L limit is re-rendered into
		 * <path>/00Tree.html and flips htmldescend sticky (tree list.c:209). */
		if (at_boundary && o->rerun && c->r->rerun) {
			c->r->rerun(c->rctx, c->path.data, o);
			htmldescend = 10;
		}

		c->r->line->entry(c->rctx, e, c->path.data, depth, is_last,
				  descend + htmldescend);

		if (descend) {
			struct asp_dir *cd;
			if (diropen_at_deep(dirfd, e->name, &cd) == 0) {
				c->r->line->newline(c->rctx);
				if (e->info)
					c->r->line->comment(c->rctx, e, depth);
				walk_dir(c, cd, depth + 1);
				asp_dirclose(cd);
			} else {
				c->r->line->error(c->rctx, "error opening dir");
				c->r->line->newline(c->rctx);
				if (e->info)
					c->r->line->comment(c->rctx, e, depth);
				(*c->errors)++;
			}
		} else {
			if (post_err)
				c->r->line->error(c->rctx, post_err);
			c->r->line->newline(c->rctx);
			if (e->info)
				c->r->line->comment(c->rctx, e, depth);
		}

		c->path.len = pathlen;
		c->path.data[pathlen] = '\0';
	}

	if (inf)
		infostack_pop(&c->istack);
	if (ig)
		gitstack_pop(&c->fstack);
	arena_rewind(&c->arena, mk);
}

/* Build the full subtree of an open directory into arena entries (->child set
 * for descended dirs). du aggregation happens later (post-prune). */
static struct entry **build_level(struct wctx *c, struct asp_dir *d, int depth,
				  int suppress_pat, struct entry *owner)
{
	const struct options *o = c->o;
	struct evec *ev = evec_at(c, depth);
	int dirfd = asp_dirfd(d);
	size_t pathlen = c->path.len;
	struct ignorefile *ig = push_dir_gitignore(c);
	struct infofile *inf = push_dir_info(c);

	read_level(c, d, ev, suppress_pat);

	/* Sort BEFORE the descent loop. The full-tree path defers display sorting to
	 * emit_level, but -l cycle detection (the inoset add/has below) is order-
	 * sensitive: tree sorts each level then walks, so a cross-directory recursive
	 * symlink is resolved against siblings in *sorted* order. Building in raw
	 * readdir order made the descent decision depend on the filesystem's entry
	 * order (inode-dependent → nondeterministic 7/4-vs-5/2 under -J/-X/-l). The
	 * streaming walk_dir already sorts here; build_level must too. emit_level's
	 * later re-sort is then idempotent (unique names → a stable total order). */
	asp_sort(ev->v, ev->n, o);

	/* --filelimit: a directory with more than N listable entries is not opened;
	 * it shows the marker and its contents are skipped. Handled uniformly for a
	 * child (owner->err, rendered as an error node) and the ROOT itself (SR-2.12:
	 * owner==NULL -> c->root_err, which asp_walk renders as a directory + that
	 * error, counted as one directory). aspen is consistent across modes;
	 * tree's --du/-d root variants diverge (deviation, see .docs/deviations.md). */
	if (o->filelimit > 0 && ev->n > (size_t)o->filelimit) {
		char m[80];
		snprintf(m, sizeof m, "%zu entries exceeds filelimit, not opening dir", ev->n);
		const char *msg = arena_strdup(&c->arena, m);
		/* A child over-limit dir shows the marker via owner->err; the ROOT itself
		 * (owner==NULL, SR-2.12) signals via wctx so asp_walk renders it as a
		 * directory + this error, counted as one directory. */
		if (owner)
			owner->err = msg;
		else
			c->root_err = msg;
		/* DEVIATION D1: a tripped filelimit is an error -> exit 2, in EVERY mode.
		 * tree exits 2 only on its streaming path and wrongly reports 0 under
		 * --du/--prune/--matchdirs; aspen is consistent. See .docs/deviations.md. */
		(*c->errors)++;
		if (inf)
			infostack_pop(&c->istack);
		if (ig)
			gitstack_pop(&c->fstack);
		return NULL; /* no children: emit_level/asp_walk shows the marker (pool keeps ev) */
	}

	/* Cache buffer + count: the per-child build_level recursion below can
	 * evec_at()->realloc the pool array, but not this level's v buffer. */
	struct entry **vec = ev->v;
	size_t n = ev->n;
	for (size_t i = 0; i < n; i++) {
		struct entry *e = vec[i];
		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);

		/* --matchdirs: a dir whose name matches -P shows its contents
		 * unfiltered and is protected from --prune. */
		int child_suppress = suppress_pat;
		if (o->matchdirs && o->npat && dir_like &&
		    pat_match_full(c, o->patterns, o->npat, e->name, e->namelen, 1)) {
			e->flags |= ENT_MATCHED;
			child_suppress = 1;
		}

		const char *post_err;
		int at_boundary; /* full-tree: emit_level finds the -R boundary via a NULL child */
		int descend = decide_descent(c, e, depth, &post_err, &at_boundary);
		(void)at_boundary;

		if (descend) {
			struct asp_dir *cd;
			if (diropen_at_deep(dirfd, e->name, &cd) == 0) {
				/* track path for child .gitignore + filtercheck */
				dstr_appendc(&c->path, '/');
				dstr_append(&c->path, e->name, e->namelen);
				e->child = build_level(c, cd, depth + 1, child_suppress, e);
				c->path.len = pathlen;
				c->path.data[pathlen] = '\0';
				asp_dirclose(cd);
			} else {
				e->err = "error opening dir";
				(*c->errors)++;
			}
		} else if (post_err) {
			/* post_err here is only ever "recursive, not followed" (the
			 * inoset cycle case); flag it so -J/-X close it with a
			 * depth-scaled indent like tree (descend==-1), not the fixed
			 * unreadable-dir close. */
			e->err = post_err;
			e->flags |= ENT_RECURSIVE;
		}
	}

	if (inf)
		infostack_pop(&c->istack);
	if (ig)
		gitstack_pop(&c->fstack);

	struct entry **arr = arena_alloc(&c->arena, (n + 1) * sizeof *arr);
	for (size_t i = 0; i < n; i++)
		arr[i] = vec[i];
	arr[n] = NULL;
	return arr; /* pool keeps ev's buffer for the next sibling */
}

/* --condense: collapse a chain of singleton directories (a dir whose only child
 * is itself a directory) onto `e`, mirroring tree's is_singleton/condensed loop.
 * The head must be a real directory (tree only condenses in its non-symlink
 * branch); the absorbed child may be a followed symlink-to-dir. e->child must be
 * fully condensed/pruned already (callers run this bottom-up). Under --du the
 * absorbed dir's own inode size is folded in, since du_aggregate runs afterward
 * over the collapsed tree and would otherwise lose it. */
static void condense_entry(struct arena *a, const struct options *o, struct entry *e)
{
	while (e->child && e->child[0] && e->child[1] == NULL) {
		struct entry *only = e->child[0];
		int only_dir = only->type == ASP_DIR ||
			       (only->type == ASP_LNK && only->ltype == ASP_DIR);
		if (!only_dir)
			break;
		const char *base = e->condensed_name ? e->condensed_name : e->name;
		const char *tail = only->condensed_name ? only->condensed_name : only->name;
		size_t bl = strlen(base), tl = strlen(tail);
		char *joined = arena_alloc(a, bl + 1 + tl + 1);
		memcpy(joined, base, bl);
		joined[bl] = '/';
		memcpy(joined + bl + 1, tail, tl);
		joined[bl + 1 + tl] = '\0';
		e->condensed_name = joined;
		if (o->duflag && e->st && only->st)
			((struct asp_statinfo *)e->st)->size += only->st->size;
		e->child = only->child;
		e->condensed += 1 + only->condensed;
	}
}

/* Bottom-up pass for --condense and/or --prune, interleaved exactly as tree does
 * it inside getfulltree: a node's subtree is settled first (recursion), then the
 * node is condensed (so it sees its children already pruned — a dir that lost all
 * but one child to pruning becomes a fresh singleton), then the parent prunes it
 * if it ended up empty. do_prune mirrors the caller's `--prune && !-d` guard. */
static void condense_prune_level(struct entry **arr, struct arena *a,
				 const struct options *o, int do_prune)
{
	size_t n = 0, w = 0;
	while (arr[n])
		n++;
	for (size_t i = 0; i < n; i++) {
		struct entry *e = arr[i];
		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like && e->child)
			condense_prune_level(e->child, a, o, do_prune);
		if (o->condense && e->type == ASP_DIR)
			condense_entry(a, o, e);
		if (do_prune) {
			int empty = dir_like && (!e->child || e->child[0] == NULL);
			if (empty && !(e->flags & ENT_MATCHED))
				continue; /* drop empty dir */
		}
		arr[w++] = e;
	}
	arr[w] = NULL;
}

/* --du: bottom-up size aggregation over the (post-prune) tree. Each directory's
 * displayed size becomes its own inode size plus the sum of its contents, like
 * tree (which accumulates into the dir's st_size). Returns this level's total. */
/* off_t accumulation, exactly as tree does it. Overflow needs a subtree summing
 * past OFF_MAX (~9 EiB on a 64-bit off_t) — not constructible on any real
 * filesystem, so this matches tree on every reachable input (SR-4.7). */
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

	/* -R sticky htmldescend, mirroring walk_dir / tree list.c:148. */
	int htmldescend = 0;

	for (size_t i = 0; i < n; i++) {
		struct entry *e = arr[i];
		int is_last = (i + 1 == n);

		/* --condense: the path's last segment is the collapsed "a/b/c" so that
		 * -f and the children's paths reflect the joined chain (tree's name). */
		dstr_appendc(&c->path, '/');
		if (e->condensed_name)
			dstr_appendz(&c->path, e->condensed_name);
		else
			dstr_append(&c->path, e->name, e->namelen);

		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like)
			c->tot->dirs += 1 + e->condensed; /* absorbed singletons count too */
		else
			c->tot->files++;

		/* -R: a dir_like with no children (->child NULL, no error) stopped at the
		 * -L boundary (not xdev-excluded) is re-rendered into <path>/00Tree.html;
		 * a descended dir keeps ->child (empty dirs get a non-NULL empty array). */
		int rd_descend = (e->child != NULL) ? 1 : 0;
		if (dir_like && !e->child && !e->err && o->rerun && o->level >= 0 &&
		    depth >= o->level &&
		    !(o->xdev && e->type == ASP_DIR && e->dev != c->root_dev) && c->r->rerun) {
			c->r->rerun(c->rctx, c->path.data, o);
			htmldescend = 10;
		}

		c->r->line->entry(c->rctx, e, c->path.data, depth, is_last,
				  rd_descend + htmldescend);

		if (e->child) {
			c->r->line->newline(c->rctx);
			if (e->info)
				c->r->line->comment(c->rctx, e, depth);
			emit_level(c, e->child, depth + 1);
		} else {
			if (e->err)
				c->r->line->error(c->rctx, e->err);
			c->r->line->newline(c->rctx);
			if (e->info)
				c->r->line->comment(c->rctx, e, depth);
		}

		c->path.len = pathlen;
		c->path.data[pathlen] = '\0';
	}
}

/* --- --fromfile / --fromtabfile: build entries from a synthetic hierarchy --- */

/* Convert a sibling list of parsed fnodes into an arena entry array, applying
 * the same listing filters as read_level (-a/-d/-P/-I). Synthetic entries carry
 * only their type in st->mode (size/time/ids zero), matching tree's newent().
 *
 * suppress_pat mirrors tree's fprune `matched`: a directory whose name matches
 * -P shows its whole subtree unfiltered and is protected from --prune. Unlike
 * the filesystem walk this is unconditional (not gated on --matchdirs) — a tree
 * fromfile quirk we reproduce. While suppressed, both -P and -I are skipped. */
static struct entry **synth_level(struct wctx *c, struct fnode *list, int suppress_pat)
{
	const struct options *o = c->o;
	int need_st = want_entry_stat(o); /* --du is part of meta_wanted */
	struct evec ev = { NULL, 0, 0 };

	for (struct fnode *fn = list; fn; fn = fn->next) {
		int isdir = fn->isdir;
		enum asp_type t = fn->islink ? ASP_LNK : (isdir ? ASP_DIR : ASP_REG);

		if (!o->all && fn->name[0] == '.')
			continue;
		if (o->dirsonly && !isdir)
			continue;

		int matched_dir = 0;
		if (!suppress_pat) {
			if (o->npat) {
				if (!isdir) {
					if (!pat_match_any(o->patterns, o->npat, fn->name,
							   isdir, o->ignorecase))
						continue; /* files must match -P */
				} else if (pat_match_any(o->patterns, o->npat, fn->name,
							 isdir, o->ignorecase)) {
					matched_dir = 1; /* dir name match -> subtree shows */
				}
			}
			if (o->nipat && pat_match_any(o->ipatterns, o->nipat, fn->name,
						      isdir, o->ignorecase))
				continue;
		}

		struct entry *e = entry_new(&c->arena, fn->name, strlen(fn->name), t);
		if (matched_dir)
			e->flags |= ENT_MATCHED; /* prune_level keeps matched dirs */
		if (fn->islink && fn->lnk)
			e->lnk = arena_strdup(&c->arena, fn->lnk); /* ltype stays UNKNOWN */
		if (need_st) {
			struct asp_statinfo si;
			memset(&si, 0, sizeof si);
			si.mode = isdir ? S_IFDIR : (fn->islink ? S_IFLNK : S_IFREG);
			e->st = arena_memdup(&c->arena, &si, sizeof si);
		}
		if (isdir && fn->child) {
			struct entry **ch = synth_level(c, fn->child,
							suppress_pat || matched_dir);
			if (ch[0] != NULL)
				e->child = ch; /* leave NULL when empty (leaf dir) */
		}
		evec_push(&ev, e);
	}

	struct entry **arr = arena_alloc(&c->arena, (ev.n + 1) * sizeof *arr);
	for (size_t i = 0; i < ev.n; i++)
		arr[i] = ev.v[i];
	arr[ev.n] = NULL;
	free(ev.v);
	return arr;
}

static void asp_walk_fromfile(const char *arg, const struct options *o,
			      const struct renderer *r, void *ctx,
			      struct totals *tot, int *errors, int last_root)
{
	/* Root metadata is the path-list file's own lstat (".": stdin -> cwd), but
	 * tree forces the displayed size to 0 (getfulltree zeroes *size). */
	struct asp_statinfo rs;
	const struct asp_statinfo *root_st = NULL;
	if (asp_stat_at(AT_FDCWD, arg, 0, &rs) == 0) {
		rs.size = 0;
		root_st = &rs;
	}

	int open_err = 0;
	struct fnode *ftop = asp_fromfile_read(arg, o, o->fromtabfile, &open_err);

	/* lstat failure (e.g. a nonexistent path-list) is a hard error, like tree. */
	if (root_st == NULL) {
		if (r->tree)
			r->tree->tree(ctx, arg, NULL, 0, NULL, NULL, tot, last_root);
		else
			r->line->root(ctx, arg, "error opening dir", NULL);
		(*errors)++;
		asp_fnode_free(ftop);
		return;
	}

	struct wctx c;
	arena_init(&c.arena, 0);
	dstr_init(&c.path);
	dstr_appendz(&c.path, arg);
	if (o->fullpath) /* tree strips trailing '/' from the root under -f */
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
	c.fstack = NULL;
	c.istack = NULL;
	c.info_top = 0;
	c.sp = NULL; /* synthetic entries carry their own stat; no real lstat */
	c.prefetch = NULL; /* fromfile is synthetic; nothing to read-prefetch */
	wctx_scratch_init(&c);
	inoset_init(&c.seen);

	struct entry **top = synth_level(&c, ftop, 0);
	if (o->condense || (o->prune && !o->dirsonly))
		condense_prune_level(top, &c.arena, o, o->prune && !o->dirsonly);
	if (o->duflag) {
		off_t dusum = du_aggregate(top);
		rs.size = dusum; /* tree: root size = aggregate only (not real+sum) */
		tot->size = dusum;
	}

	if (r->tree) {
		r->tree->tree(ctx, arg, root_st, 1, NULL, top, tot, last_root);
	} else {
		r->line->root(ctx, arg, NULL, root_st);
		tot->dirs++; /* root counts as a directory */
		emit_level(&c, top, 1);
	}

	wctx_scratch_free(&c);
	inoset_destroy(&c.seen);
	dstr_free(&c.path);
	arena_destroy(&c.arena);
	asp_fnode_free(ftop);
}

void asp_walk(const char *root, const struct options *o, const struct renderer *r,
	      void *ctx, struct totals *tot, int *errors, struct statprov *sp,
	      int last_root)
{
	if (o->fromfile || o->fromtabfile) {
		asp_walk_fromfile(root, o, r, ctx, tot, errors, last_root);
		return;
	}

	struct asp_dir *d;
	if (asp_diropen(root, &d) != 0) {
		/* tree: lstat the root. If it exists (a non-directory, or a directory we
		 * cannot open) it is shown "[error opening dir]" and counted as ONE FILE
		 * with rc 0; only a root that does not stat at all is an error (rc 2).
		 * Nested renderers type the failed root from this stat (e.g. "file"). */
		struct asp_statinfo rs2;
		const struct asp_statinfo *fst =
			(asp_stat_at(AT_FDCWD, root, 0, &rs2) == 0) ? &rs2 : NULL;
		if (r->tree)
			r->tree->tree(ctx, root, fst, 0, NULL, NULL, tot, last_root);
		else
			r->line->root(ctx, root, "error opening dir", fst);
		if (fst)
			tot->files++;
		else
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
	c.fstack = NULL;
	c.istack = NULL;
	c.info_top = 0;
	inoset_init(&c.seen);
	wctx_scratch_init(&c);
	c.sp = sp; /* shared stat backend, built once in render_tree (SR-2.5) */
	/* SR-3.5 cross-dir read-prefetch: opt-in (ASP_PREFETCH), default off so warm
	 * runs pay nothing. Sized like the stat pool; --threads bounds it too. Only the
	 * streaming walk_dir prefetches, so don't build the pool for the full-tree
	 * engines (-J/-X via r->tree, or --du/--prune/--matchdirs/--condense/--filelimit
	 * via needfulltree) where it would spawn ~N idle threads (SR02-2.5). */
	c.prefetch = NULL;
	if (getenv("ASP_PREFETCH") && r->line && !needfulltree(o)) {
		int w = (o->threads > 0) ? o->threads : asp_pool_default_workers();
		if (w > 1)
			c.prefetch = asp_pool_create(w);
	}

	/* Bottom of the filter stack: an explicit --gitfile and, with --gitignore,
	 * $GIT_DIR/info/exclude. (The implicit parent-.gitignore walk is deferred.) */
	if (o->gitfile)
		gitstack_push(&c.fstack, gitignore_load_file(".", o->gitfile));
	if (o->gitignore) {
		const char *gd = getenv("GIT_DIR");
		if (gd) {
			char ex[PATH_MAX + sizeof "/info/exclude"]; /* GIT_DIR path + suffix */
			snprintf(ex, sizeof ex, "%s/info/exclude", gd);
			gitstack_push(&c.fstack, gitignore_load_file(gd, ex));
		}
	}

	/* --infofile (explicit) and, with --info, the global info file. */
	if (o->infofile)
		infostack_push(&c.istack, info_load_file(o->infofile));
	if (o->showinfo)
		infostack_push(&c.istack, info_load_file(ASP_INFO_PATH));

	/* The root needs up to two stats, divergent only when it is a symlink:
	 *   walk stat — the OPENED directory ('.' via the dir fd, symlinks followed):
	 *               supplies the device for -x and the ino/dev cycle seed for -l,
	 *               i.e. the directory actually being traversed.
	 *   display stat (rs) — lstat(root): what the renderer SHOWS (metadata
	 *               bracket, color, -F suffix). tree displays the link's own
	 *               metadata ('@', link size/perms) even though it walks the
	 *               target; for a real-dir root the two stats are identical. */
	if (o->xdev || o->follow) {
		struct asp_statinfo ws;
		if (asp_stat_at(asp_dirfd(d), ".", 1, &ws) == 0) {
			c.root_dev = ws.dev;
			if (o->follow)
				inoset_add(&c.seen, ws.ino, ws.dev);
		}
	}
	struct asp_statinfo rs;
	const struct asp_statinfo *root_st = NULL;
	if (root_needs_stat(o)) {
		if (asp_stat_at(AT_FDCWD, root, 0, &rs) == 0)
			root_st = &rs;
	}

	if (r->tree) {
		/* Nested formats (JSON/XML): build the whole tree, hand it off. The
		 * renderer counts entries and emits; we just compute the du total. */
		struct entry **top = build_level(&c, d, 1, 0, NULL);
		if (c.root_err) { /* SR-2.12: root itself over --filelimit */
			r->tree->tree(ctx, root, root_st, 1, c.root_err, NULL, tot, last_root);
		} else {
			if (o->condense || (o->prune && !o->dirsonly))
				condense_prune_level(top, &c.arena, o, o->prune && !o->dirsonly);
			if (o->duflag) {
				off_t dusum = du_aggregate(top);
				if (root_st) {
					rs.size += dusum;
					tot->size = rs.size;
				} else {
					tot->size = dusum;
				}
			}
			r->tree->tree(ctx, root, root_st, 1, NULL, top, tot, last_root);
		}
	} else if (needfulltree(o)) {
		struct entry **top = build_level(&c, d, 1, 0, NULL);
		if (c.root_err) { /* SR-2.12: root itself over --filelimit -> 1 dir */
			r->line->root(ctx, root, c.root_err, root_st);
			tot->dirs++;
		} else {
			if (o->condense || (o->prune && !o->dirsonly))
				condense_prune_level(top, &c.arena, o, o->prune && !o->dirsonly);
			if (o->duflag) {
				off_t dusum = du_aggregate(top);
				if (root_st) { /* root total = root's own size + contents */
					rs.size += dusum;
					tot->size = rs.size;
				} else {
					tot->size = dusum;
				}
			}
			r->line->root(ctx, root, NULL, root_st);
			if (top[0]) /* tree counts the root as a directory only when non-empty */
				tot->dirs++;
			emit_level(&c, top, 1);
		}
	} else {
		/* Streaming: we can't see emptiness up front, so count the root as a
		 * directory only if the walk displayed at least one child (any displayed
		 * descendant means the root listing was non-empty), matching tree. */
		unsigned long before = tot->dirs + tot->files;
		r->line->root(ctx, root, NULL, root_st);
		walk_dir(&c, d, 1);
		if (tot->dirs + tot->files > before)
			tot->dirs++;
	}

	asp_dirclose(d);
	gitstack_flush(&c.fstack); /* c.sp is owned by render_tree, not freed here */
	infostack_flush(&c.istack);
	asp_pool_destroy(c.prefetch); /* SR-3.5: per-walk prefetch pool (NULL = no-op) */
	wctx_scratch_free(&c);
	inoset_destroy(&c.seen);
	dstr_free(&c.path);
	arena_destroy(&c.arena);
}
