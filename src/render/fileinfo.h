#ifndef ASP_RENDER_FILEINFO_H
#define ASP_RENDER_FILEINFO_H

/* The bracketed metadata column: [inode][dev][perms][user][group][size][date].
 * Faithful to tree's fillinfo/prot/psize/do_date. */

#include "options.h"
#include "sys/xstat.h"

#include <stddef.h>

/* Writes the column for `st` into buf; returns its length. buf[0] == '[' iff
 * non-empty (tree's leading-space -> '[' transform), with a trailing ']'.
 * st == NULL (no stat) yields an empty column. */
size_t asp_fillinfo(char *buf, size_t bufsz, const struct options *o,
		    const struct asp_statinfo *st);

/* Format a size into buf (tree's psize: " %4d"/" %3.1fK"/" %11lld" forms);
 * returns the byte count. Exposed for the --du report. */
int asp_psize(char *buf, const struct options *o, off_t size);

/* ls-style permission string ("drwxr-xr-x"); shared with JSON/XML. */
const char *asp_prot(mode_t m);
/* Date string (honors --timefmt / 6-month rule); shared with JSON/XML. */
const char *asp_do_date(const struct options *o, time_t t);

#endif /* ASP_RENDER_FILEINFO_H */
