#ifndef ASP_RENDER_UNIX_H
#define ASP_RENDER_UNIX_H

/* The default (unix) output renderer. */

#include "dstr.h"
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
	const struct linedraw *ld;
	unsigned char *last; /* last[depth] = is_last; grows on demand */
	size_t last_cap;
};

/* charset_name may be NULL (auto-detect via env/locale). */
void unix_ctx_init(struct unix_ctx *u, int fd, int mb_cur_max, const char *charset_name);
void unix_ctx_destroy(struct unix_ctx *u);

extern const struct renderer asp_unix_renderer;

#endif /* ASP_RENDER_UNIX_H */
