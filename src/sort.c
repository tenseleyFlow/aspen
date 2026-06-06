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
			v = strcoll(a->name, b->name);
		break;
	}
	case SORT_MTIME: {
		time_t ta = a->st ? a->st->mtime : 0, tb = b->st ? b->st->mtime : 0;
		v = (ta == tb) ? strcoll(a->name, b->name) : (ta < tb ? -1 : 1);
		break;
	}
	case SORT_CTIME: {
		time_t ta = a->st ? a->st->ctime : 0, tb = b->st ? b->st->ctime : 0;
		v = (ta == tb) ? strcoll(a->name, b->name) : (ta < tb ? -1 : 1);
		break;
	}
	case SORT_NAME:
	default:
		v = strcoll(a->name, b->name);
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
	char *key; /* strxfrm transform of e->name */
};

static int keycmp(const void *pa, const void *pb)
{
	const struct keyed *a = pa, *b = pb;
	int v = strcmp(a->key, b->key);
	return SO->reverse ? -v : v;
}

void asp_sort(struct entry **v, size_t n, const struct options *o)
{
	if (o->sort == SORT_NONE) /* -U: unsorted, and disables the meta-sort */
		return;
	SO = o;

	/* Fast path for the common case (plain name sort in a collating locale):
	 * transform each name once with strxfrm, then sort keys with memcmp —
	 * O(n) strxfrm instead of O(n log n) strcoll. Same order by definition.
	 * Skipped for C/POSIX (strcoll is already byte compare) and when a
	 * meta-sort needs the dir/file split. */
	if (o->sort == SORT_NAME && !o->dirsfirst && !o->filesfirst && n > 1 &&
	    !c_collate()) {
		struct keyed *k = asp_xmalloc(n * sizeof *k);
		for (size_t i = 0; i < n; i++) {
			size_t need = strxfrm(NULL, v[i]->name, 0);
			k[i].e = v[i];
			k[i].key = asp_xmalloc(need + 1);
			strxfrm(k[i].key, v[i]->name, need + 1);
		}
		qsort(k, n, sizeof *k, keycmp);
		for (size_t i = 0; i < n; i++) {
			v[i] = k[i].e;
			free(k[i].key);
		}
		free(k);
		return;
	}

	qsort(v, n, sizeof *v, cmp);
}
