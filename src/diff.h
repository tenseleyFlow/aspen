#ifndef ASP_DIFF_H
#define ASP_DIFF_H

#include "entry.h"
#include "options.h"

enum diff_status {
	DIFF_ADDED,
	DIFF_REMOVED,
	DIFF_MODIFIED,
	DIFF_UNCHANGED,
};

struct diff_entry {
	enum diff_status status;
	const struct entry *a;
	const struct entry *b;
	struct diff_entry **child;
};

struct diff_totals {
	unsigned long added;
	unsigned long removed;
	unsigned long modified;
	unsigned long unchanged;
};

int asp_diff_run(const char *dir_a, const char *dir_b,
		 const struct options *o, int outfd, int mb);

#endif /* ASP_DIFF_H */
