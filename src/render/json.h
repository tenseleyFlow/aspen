#ifndef ASP_RENDER_JSON_H
#define ASP_RENDER_JSON_H

/* JSON output (-J), faithful to tree's json.c. Builds the full tree (via the
 * renderer's `tree` hook) and emits a nested array. */

#include "dstr.h"
#include "options.h"
#include "render.h"

struct json_ctx {
	struct dstr out;
	int fd;
	const struct options *o;
	int seen_err; /* tree quirk: once any error occurs, -J gives every later
		       * entry a spurious empty "contents":[    ] (flag.J && errors) */
};

void json_ctx_init(struct json_ctx *j, int fd, const struct options *o);
void json_ctx_destroy(struct json_ctx *j);

extern const struct renderer asp_json_renderer;

#endif /* ASP_RENDER_JSON_H */
