#include "sort.h"
#include "util.h"
#include "verscmp.h"
#include "sys/xstat.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

/* qsort isn't portably reentrant; the engine is single-threaded, so a file-scope
 * context set immediately before qsort is safe. */
static const struct options *SO;
/* C/POSIX collation == byte order, so strcoll there is just strcmp with locale
 * overhead on every call. Resolve once per sort and use strcmp when true. */
static int SO_cc;

static int namecmp(const char *a, const char *b)
{
	return SO_cc ? strcmp(a, b) : strcoll(a, b);
}

static int dir_like(const struct entry *e)
{
	return e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
}

static int basecmp(const struct entry *a, const struct entry *b)
{
	int v;
	switch (SO->sort) {
	case SORT_VERSION:
		v = asp_verscmp(a->name, b->name);
		break;
	case SORT_SIZE: {
		off_t sa = a->st ? a->st->size : 0, sb = b->st ? b->st->size : 0;
		v = (sa == sb) ? 0 : (sa < sb ? 1 : -1); /* larger first, like tree */
		if (v == 0)
			v = namecmp(a->name, b->name);
		break;
	}
	case SORT_MTIME: {
		time_t ta = a->st ? a->st->mtime : 0, tb = b->st ? b->st->mtime : 0;
		v = (ta == tb) ? namecmp(a->name, b->name) : (ta < tb ? -1 : 1);
		break;
	}
	case SORT_CTIME: {
		time_t ta = a->st ? a->st->ctime : 0, tb = b->st ? b->st->ctime : 0;
		v = (ta == tb) ? namecmp(a->name, b->name) : (ta < tb ? -1 : 1);
		break;
	}
	case SORT_NAME:
	default:
		v = namecmp(a->name, b->name);
		break;
	}
	return SO->reverse ? -v : v;
}

static int cmp(const void *pa, const void *pb)
{
	const struct entry *a = *(const struct entry *const *)pa;
	const struct entry *b = *(const struct entry *const *)pb;
	if (SO->dirsfirst || SO->filesfirst) {
		int da = dir_like(a), db = dir_like(b);
		if (da != db) {
			if (SO->dirsfirst)
				return da ? -1 : 1;
			return da ? 1 : -1; /* filesfirst */
		}
	}
	return basecmp(a, b);
}

/* True when collation is byte-order (C/POSIX): strcoll is already cheap there. */
static int c_collate(void)
{
	const char *l = setlocale(LC_COLLATE, NULL);
	return !l || !strcmp(l, "C") || !strcmp(l, "POSIX");
}

struct keyed {
	struct entry *e;
	size_t koff; /* offset of this name's strxfrm key in the packed buffer */
};

static char *SO_keys; /* base of the packed key buffer, for keycmp */

static int keycmp(const void *pa, const void *pb)
{
	const struct keyed *a = pa, *b = pb;
	int v = strcmp(SO_keys + a->koff, SO_keys + b->koff);
	return SO->reverse ? -v : v;
}

void asp_sort(struct entry **v, size_t n, const struct options *o)
{
	if (o->sort == SORT_NONE) /* -U: unsorted, and disables the meta-sort */
		return;
	SO = o;
	SO_cc = c_collate();

	/* Fast path for the common case (plain name sort in a collating locale):
	 * transform each name once with strxfrm, then sort keys with memcmp —
	 * O(n) strxfrm instead of O(n log n) strcoll. Same order by definition.
	 * Skipped for C/POSIX (strcoll is already byte compare) and when a
	 * meta-sort needs the dir/file split. */
	if (o->sort == SORT_NAME && !o->dirsfirst && !o->filesfirst && n > 1 &&
	    !SO_cc) {
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
		SO_keys = buf;
		qsort(k, n, sizeof *k, keycmp);
		for (size_t i = 0; i < n; i++)
			v[i] = k[i].e;
		SO_keys = NULL;
		free(buf);
		free(k);
		return;
	}

	qsort(v, n, sizeof *v, cmp);
}
