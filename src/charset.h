#ifndef ASP_CHARSET_H
#define ASP_CHARSET_H

/*
 * Line-drawing charset selection. Each connector carries tree 2.3.2's three
 * width-forms ([0] widest .. [2] narrowest), indexed by the --compress level;
 * the default ([0]) plus indent()'s trailing space reproduces 2.3.2's output.
 * Selection: -A => ANSI; else --charset/-S, then $TREE_CHARSET, then a UTF-8
 * locale, else ASCII fallback.
 */

#include "options.h"

struct linedraw {
	const char *vert[3];      /* continuing-ancestor glyph, by --compress level */
	const char *vert_left[3]; /* branch (non-last entry) */
	const char *corner[3];    /* last entry */
	/* .info comment decorators (tree's ctop/cbot/cmid/cext/csingle) */
	const char *ctop, *cbot, *cmid, *cext, *csingle;
};

const struct linedraw *asp_linedraw(const struct options *o);

/* Effective charset name: --charset/-S, then $TREE_CHARSET, then "UTF-8" in a
 * UTF-8 locale, else NULL. Used for the XML encoding attribute. */
const char *asp_charset_name(const struct options *o);

#endif /* ASP_CHARSET_H */
