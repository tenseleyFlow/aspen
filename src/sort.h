#ifndef ASP_SORT_H
#define ASP_SORT_H

/* Sort a directory's entries per the active options: base sort
 * (name/version/size/mtime/ctime/none), the dirsfirst/filesfirst meta-sort, and
 * -r reverse. SORT_NONE leaves readdir order (and disables the meta-sort, like
 * tree's -U). Mirrors tree's comparators exactly. */

#include "entry.h"
#include "options.h"

#include <stddef.h>

/* Reusable scratch for asp_sort, grown high-water-mark and shared across the many
 * per-directory sorts of one walk so the engine doesn't malloc/free a buffer per
 * level (CLAUDE.md "no malloc churn"). Zero-initialise, hand the SAME handle to
 * every asp_sort call, then asp_sort_scratch_free() once at teardown. asp_sort is
 * single-threaded (sorting is never parallelized — determinism), so one handle per
 * walk is safe even across the recursive level calls. Pass NULL for a one-off sort
 * (asp_sort then uses a private buffer and frees it). Members are void* because the
 * key record type is private to sort.c. */
struct asp_sort_scratch {
	void *aux;    size_t aux_cap;   /* entry* array for the MSD-radix pass */
	void *keys;   size_t keys_cap;  /* key records for the strxfrm pass */
	char *buf;    size_t buf_cap;   /* packed strxfrm key bytes */
};

void asp_sort(struct entry **v, size_t n, const struct options *o,
	      struct asp_sort_scratch *sc);
void asp_sort_scratch_free(struct asp_sort_scratch *sc);

#endif /* ASP_SORT_H */
