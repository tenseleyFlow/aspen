#include "render.h"
#include "traverse.h"

int render_tree(const char *const *dirs, const struct options *opts,
		const struct renderer *r, void *ctx, struct totals *tot)
{
	int errors = 0;
	struct totals t = { 0, 0, 0 };

	/* One stat backend for all roots (SR-2.5): created here, not per-root. */
	struct statprov *sp = asp_statprov_create(opts);

	r->begin(ctx);
	for (size_t i = 0; dirs[i]; i++)
		asp_walk(dirs[i], opts, r, ctx, &t, &errors, sp, dirs[i + 1] == NULL);
	if (!opts->noreport) /* --noreport: omit the report in every renderer (was unix-only) */
		r->report(ctx, &t);
	r->end(ctx);
	asp_statprov_destroy(sp);

	if (tot)
		*tot = t;
	return errors ? 2 : 0;
}
