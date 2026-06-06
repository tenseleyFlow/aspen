#ifndef ASP_COLOR_H
#define ASP_COLOR_H

/*
 * LS_COLORS / TREE_COLORS colorization, faithful to tree's color.c: the enable
 * decision (NO_COLOR / TERM / CLICOLOR[_FORCE] / TTY / -C / -n), the verbatim
 * built-in default map, and color()'s mode/type/extension match order.
 * Renders escape codes into the output dstr (we buffer; tree fputs).
 */

#include "dstr.h"
#include "options.h"

#include <sys/stat.h>

struct cext;

struct colorizer {
	int enabled;
	int linktargetcolor;
	const char **code;  /* indexed by COL_*; NULL if unset */
	struct cext *ext;   /* extension list (newest first, like tree) */
	char *buf;          /* owned copy of the colors string (codes point into it) */
};

/* outfd is the fd color/TTY detection applies to (tree uses fd 1). */
void color_init(struct colorizer *c, const struct options *o, int outfd);
void color_free(struct colorizer *c);

/* Emit the color for a name; returns 1 if a code was written (=> call color_end). */
int color_apply(struct colorizer *c, struct dstr *out, mode_t mode,
		const char *name, int orphan, int islink);
void color_end(struct colorizer *c, struct dstr *out);

#endif /* ASP_COLOR_H */
