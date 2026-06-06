#ifndef ASP_RENDER_HTML_H
#define ASP_RENDER_HTML_H

/* HTML output (-H). Line-oriented (flat <a>...</a><br> per entry, like unix),
 * faithful to tree's html.c. -R (recursive 00Tree.html) is deferred (ledger). */

#include "charset.h"
#include "dstr.h"
#include "options.h"
#include "render.h"

struct html_ctx {
	struct dstr out;
	int fd;
	int mb_cur_max;
	const struct options *o;
	const struct linedraw *ld;
	const char *charset; /* meta charset, or NULL */
	unsigned char *last;
	size_t last_cap;
	size_t htmldirlen; /* strlen of current root arg */
};

void html_ctx_init(struct html_ctx *h, int fd, int mb_cur_max, const struct options *o);
void html_ctx_destroy(struct html_ctx *h);

extern const struct renderer asp_html_renderer;

#endif /* ASP_RENDER_HTML_H */
