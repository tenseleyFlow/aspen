#ifndef ASP_RENDER_UNIX_H
#define ASP_RENDER_UNIX_H

/* The default (unix) output renderer. */

#include "color.h"
#include "dstr.h"
#include "options.h"
#include "render.h"

struct linedraw {
	const char *vert;      /* continuing-ancestor glyph */
	const char *vert_left; /* branch (non-last entry) */
	const char *corner;    /* last entry */
};

struct unix_ctx {
	struct dstr out;
	int fd;
	int mb_cur_max;
	int np_flags; /* name_print flags from -q/-N/-Q */
	const struct options *o;
	struct colorizer *col;
	const struct linedraw *ld;
	unsigned char *last; /* last[depth] = is_last; grows on demand */
	size_t last_cap;
};

void unix_ctx_init(struct unix_ctx *u, int fd, int mb_cur_max,
		   const struct options *o, struct colorizer *col);
void unix_ctx_destroy(struct unix_ctx *u);

extern const struct renderer asp_unix_renderer;

#endif /* ASP_RENDER_UNIX_H */
