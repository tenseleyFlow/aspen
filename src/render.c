#include "render.h"
#include "traverse.h"

int render_tree(const char *const *dirs, const struct options *opts,
		const struct renderer *r, void *ctx, struct totals *tot)
{
	int errors = 0;
	struct totals t = { 0, 0 };

	r->begin(ctx);
	for (size_t i = 0; dirs[i]; i++)
		asp_walk(dirs[i], opts, r, ctx, &t, &errors);
	r->report(ctx, &t);
	r->end(ctx);

	if (tot)
		*tot = t;
	return errors ? 2 : 0;
}
