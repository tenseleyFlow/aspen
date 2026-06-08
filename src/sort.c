#include "sort.h"
#include "util.h"
#include "verscmp.h"
#include "sys/xstat.h"
#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

/* Per-sort context, passed through qsort_r — no file-scope mutable state. */
struct sortctx {
	const struct options *o;
	int cc;           /* C/POSIX collation == byte order, so strcmp suffices */
	const char *keys; /* strxfrm key buffer base (keyed fast path), else NULL */
};

/* Reentrant qsort. cmp is GNU/C23-style (a, b, ctx); we adapt the BSD signature
 * (thunk first) with a trampoline, and fall back to a single-thread file-scope
 * context only on a libc with no qsort_r at all (the engine never sorts
 * concurrently, so that fallback is safe). */
typedef int (*asp_cmp_fn)(const void *, const void *, void *);

#if ASP_HAS_QSORT_R_GNU
static void asp_qsort_r(void *b, size_t n, size_t sz, asp_cmp_fn cmp, void *ctx)
{
	qsort_r(b, n, sz, cmp, ctx);
}
#elif ASP_HAS_QSORT_R_BSD
struct bsd_tramp {
	asp_cmp_fn cmp;
	void *ctx;
};
static int bsd_thunk(void *t, const void *a, const void *b)
{
	struct bsd_tramp *tr = t;
	return tr->cmp(a, b, tr->ctx);
}
static void asp_qsort_r(void *b, size_t n, size_t sz, asp_cmp_fn cmp, void *ctx)
{
	struct bsd_tramp tr = { cmp, ctx };
	qsort_r(b, n, sz, &tr, bsd_thunk);
}
#else
static asp_cmp_fn g_cmp;
static void *g_ctx;
static int plain_thunk(const void *a, const void *b) { return g_cmp(a, b, g_ctx); }
static void asp_qsort_r(void *b, size_t n, size_t sz, asp_cmp_fn cmp, void *ctx)
{
	g_cmp = cmp;
	g_ctx = ctx;
	qsort(b, n, sz, plain_thunk);
}
#endif

static int namecmp(const struct sortctx *c, const char *a, const char *b)
{
	return c->cc ? strcmp(a, b) : strcoll(a, b);
}

static int dir_like(const struct entry *e)
{
	return e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
}

static int basecmp(const struct sortctx *c, const struct entry *a, const struct entry *b)
{
	int v;
	switch (c->o->sort) {
	case SORT_VERSION:
		v = asp_verscmp(a->name, b->name);
		break;
	case SORT_SIZE: {
		off_t sa = a->st ? a->st->size : 0, sb = b->st ? b->st->size : 0;
		v = (sa == sb) ? 0 : (sa < sb ? 1 : -1); /* larger first, like tree */
		if (v == 0)
			v = namecmp(c, a->name, b->name);
		break;
	}
	case SORT_MTIME: {
		time_t ta = a->st ? a->st->mtime : 0, tb = b->st ? b->st->mtime : 0;
		v = (ta == tb) ? namecmp(c, a->name, b->name) : (ta < tb ? -1 : 1);
		break;
	}
	case SORT_CTIME: {
		time_t ta = a->st ? a->st->ctime : 0, tb = b->st ? b->st->ctime : 0;
		v = (ta == tb) ? namecmp(c, a->name, b->name) : (ta < tb ? -1 : 1);
		break;
	}
	case SORT_NAME:
	default:
		v = namecmp(c, a->name, b->name);
		break;
	}
	return c->o->reverse ? -v : v;
}

static int cmp(const void *pa, const void *pb, void *vc)
{
	const struct sortctx *c = vc;
	const struct entry *a = *(const struct entry *const *)pa;
	const struct entry *b = *(const struct entry *const *)pb;
	if (c->o->dirsfirst || c->o->filesfirst) {
		int da = dir_like(a), db = dir_like(b);
		if (da != db) {
			if (c->o->dirsfirst)
				return da ? -1 : 1;
			return da ? 1 : -1; /* filesfirst */
		}
	}
	return basecmp(c, a, b);
}

/* True when collation is byte-order (strcoll == memcmp): C/POSIX by name, plus
 * locales like C.UTF-8 that collate by code point but aren't named "C". Detected
 * with a strxfrm identity probe — if strxfrm is the identity transform then
 * strcoll must equal memcmp (the strxfrm contract), so strcmp sorting is exact
 * and the O(n) strxfrm key pass can be skipped. A false negative only costs
 * speed (keeps the strxfrm path); it never reorders, so parity is safe. */
static int c_collate(void)
{
	/* LC_COLLATE is set once at startup (main) and never changes, so the probe's
	 * result is process-invariant — cache it instead of re-running setlocale + a
	 * strxfrm probe per directory (SR02-1.2). -1 = not yet computed. */
	static int cached = -1;
	if (cached >= 0)
		return cached;

	const char *l = setlocale(LC_COLLATE, NULL);
	if (!l || !strcmp(l, "C") || !strcmp(l, "POSIX"))
		return (cached = 1);
	/* Mixed case, digits, an accented UTF-8 byte pair, and a control byte: any
	 * case- or accent-folding collation breaks byte identity here. */
	static const char probe[] = "\tAaZz09\xc3\xa9";
	char buf[64];
	size_t n = strxfrm(buf, probe, sizeof buf);
	return (cached = (n == sizeof probe - 1 && memcmp(buf, probe, n) == 0));
}

struct keyed {
	struct entry *e;
	size_t koff; /* offset of this name's strxfrm key in the packed buffer */
};

static int keycmp(const void *pa, const void *pb, void *vc)
{
	const struct sortctx *c = vc;
	const struct keyed *a = pa, *b = pb;
	int v = strcmp(c->keys + a->koff, c->keys + b->koff);
	return c->o->reverse ? -v : v;
}

/* MSD byte-radix sort of entry pointers by name (SR-3.1) — used only for the
 * plain-name sort in a byte-order locale (strcoll == memcmp), where it beats
 * qsort+strcmp several-fold on large directories. Names within a directory are
 * unique, so radix's instability is irrelevant. Bucket 0 marks end-of-name
 * (sorts before any byte, matching memcmp); real bytes occupy buckets 1..256. */
static int radix_key(const struct entry *e, size_t d)
{
	return d < (size_t)e->namelen ? (unsigned char)e->name[d] + 1 : 0;
}

static void radix_msd(struct entry **a, struct entry **aux, size_t n, size_t depth)
{
	if (n < 2)
		return;
	if (n <= 24) { /* small subarray: bytewise insertion sort from depth */
		for (size_t i = 1; i < n; i++) {
			struct entry *x = a[i];
			size_t j = i;
			while (j > 0 && strcmp(a[j - 1]->name + depth, x->name + depth) > 0) {
				a[j] = a[j - 1];
				j--;
			}
			a[j] = x;
		}
		return;
	}
	size_t cnt[258];
	memset(cnt, 0, sizeof cnt);
	for (size_t i = 0; i < n; i++)
		cnt[radix_key(a[i], depth) + 1]++;
	for (int r = 1; r < 258; r++)
		cnt[r] += cnt[r - 1];
	size_t start[258];
	memcpy(start, cnt, sizeof start); /* bucket starts (cnt is mutated below) */
	for (size_t i = 0; i < n; i++) {
		int k = radix_key(a[i], depth);
		aux[cnt[k]++] = a[i];
	}
	memcpy(a, aux, n * sizeof *a);
	for (int k = 1; k <= 256; k++) { /* recurse byte buckets; bucket 0 is <=1 entry */
		size_t lo = start[k], hi = start[k + 1];
		if (hi - lo > 1)
			radix_msd(a + lo, aux + lo, hi - lo, depth + 1);
	}
}

void asp_sort(struct entry **v, size_t n, const struct options *o)
{
	if (o->sort == SORT_NONE) /* -U: unsorted, and disables the meta-sort */
		return;
	struct sortctx c = { o, c_collate(), NULL };

	/* Fastest path: plain-name sort in a byte-order locale -> MSD byte radix
	 * (SR-3.1). Same order as memcmp/strcoll by construction; -r reverses the
	 * ascending result (names are unique, so this is exact). The meta-sort
	 * (dirs/files-first) keeps the comparator path. */
	if (o->sort == SORT_NAME && c.cc && !o->dirsfirst && !o->filesfirst && n > 1) {
		struct entry **aux = asp_xmalloc(n * sizeof *aux);
		radix_msd(v, aux, n, 0);
		free(aux);
		if (o->reverse)
			for (size_t i = 0, j = n - 1; i < j; i++, j--) {
				struct entry *t = v[i];
				v[i] = v[j];
				v[j] = t;
			}
		return;
	}

	/* Fast path for the common case (plain name sort in a collating locale):
	 * transform each name once with strxfrm, then sort keys with memcmp —
	 * O(n) strxfrm instead of O(n log n) strcoll. Same order by definition.
	 * Skipped for C/POSIX (strcoll is already byte compare) and when a
	 * meta-sort needs the dir/file split. */
	if (o->sort == SORT_NAME && !o->dirsfirst && !o->filesfirst && n > 1 &&
	    !c.cc) {
		struct keyed *k = asp_xmalloc(n * sizeof *k);
		char *buf = NULL;
		size_t cap = 0, off = 0;
		for (size_t i = 0; i < n; i++) {
			const char *name = v[i]->name;
			/* Generous guess keeps strxfrm to one call per name; the
			 * retry below only fires on a rare underestimate. Packing all
			 * keys in one buffer avoids 2n small malloc/free pairs. */
			size_t guess = (size_t)v[i]->namelen * 4 + 32;
			if (cap - off < guess) {
				cap = cap ? cap * 2 : 8192;
				while (cap - off < guess)
					cap *= 2;
				buf = asp_xrealloc(buf, cap);
			}
			size_t got = strxfrm(buf + off, name, cap - off);
			if (got >= cap - off) { /* underestimate: grow to fit, redo once */
				cap = off + got + 1;
				buf = asp_xrealloc(buf, cap);
				strxfrm(buf + off, name, cap - off);
			}
			k[i].e = v[i];
			k[i].koff = off;
			off += got + 1;
		}
		c.keys = buf;
		asp_qsort_r(k, n, sizeof *k, keycmp, &c);
		for (size_t i = 0; i < n; i++)
			v[i] = k[i].e;
		free(buf);
		free(k);
		return;
	}

	asp_qsort_r(v, n, sizeof *v, cmp, &c);
}
