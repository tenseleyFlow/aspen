#ifndef ASP_SORT_H
#define ASP_SORT_H

/* Sort a directory's entries per the active options: base sort
 * (name/version/size/mtime/ctime/none), the dirsfirst/filesfirst meta-sort, and
 * -r reverse. SORT_NONE leaves readdir order (and disables the meta-sort, like
 * tree's -U). Mirrors tree's comparators exactly. */

#include "entry.h"
#include "options.h"

#include <stddef.h>

void asp_sort(struct entry **v, size_t n, const struct options *o);

#endif /* ASP_SORT_H */
