#ifndef ASP_RENDER_UNIX_H
#define ASP_RENDER_UNIX_H

/* The default (unix) output renderer. */

#include "charset.h"
#include "color.h"
#include "dstr.h"
#include "options.h"
#include "render.h"

#include <limits.h>

struct unix_ctx {
	struct dstr out;
	int fd;
	int mb_cur_max;
	int np_flags; /* name_print flags from -q/-N/-Q */
	const struct options *o;
	struct colorizer *col;
	const struct linedraw *ld;
	/* Precomputed indent glyphs + lengths for the active --compress level, so
	 * draw_indent appends by length instead of strlen'ing each glyph per line
	 * (SR02-1.4). */
	const char *ind_vert, *ind_vleft, *ind_corner, *ind_space;
	size_t ind_vert_n, ind_vleft_n, ind_corner_n, ind_space_n;
	/* OSC-8 hyperlinks (--hyperlink) */
	int hyper;
	const char *scheme;
	char authority[256];
	char realbase[PATH_MAX]; /* realpath() result; needs >= PATH_MAX */
	size_t pathoffset;
	unsigned char *last; /* last[depth] = is_last; grows on demand */
	size_t last_cap;
};

void unix_ctx_init(struct unix_ctx *u, int fd, int mb_cur_max,
		   const struct options *o, struct colorizer *col);
void unix_ctx_destroy(struct unix_ctx *u);

extern const struct renderer asp_unix_renderer;

#endif /* ASP_RENDER_UNIX_H */
