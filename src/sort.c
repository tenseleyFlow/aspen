#include "sort.h"
#include "verscmp.h"
#include "sys/xstat.h"

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

void asp_sort(struct entry **v, size_t n, const struct options *o)
{
	if (o->sort == SORT_NONE) /* -U: unsorted, and disables the meta-sort */
		return;
	SO = o;
	qsort(v, n, sizeof *v, cmp);
}
