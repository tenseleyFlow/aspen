#ifndef ASP_CHARSET_H
#define ASP_CHARSET_H

/*
 * Line-drawing charset selection. The indent connectors are the widest ([0])
 * forms of tree 2.3.2's cstable; indent() appends one space, reproducing 2.3.2's
 * output. Selection: -A => ANSI; else --charset/-S, then $TREE_CHARSET, then a
 * UTF-8 locale, else ASCII fallback.
 */

#include "options.h"

struct linedraw {
	const char *vert;      /* continuing-ancestor glyph (widest form) */
	const char *vert_left; /* branch (non-last entry) */
	const char *corner;    /* last entry */
};

const struct linedraw *asp_linedraw(const struct options *o);

#endif /* ASP_CHARSET_H */
