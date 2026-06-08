#include "render.h"
#include "traverse.h"

int render_tree(const char *const *dirs, const struct options *opts,
		const struct renderer *r, void *ctx, struct totals *tot)
{
	int errors = 0;
	struct totals t = { 0, 0, 0 };

	/* One stat backend for all roots (SR-2.5): created here, not per-root. */
	struct statprov *sp = asp_statprov_create(opts);

	/* begin/report/end share a signature across both renderer kinds (enforced by
	 * the rdr_*_fn typedefs, SR02-2.4); pick from whichever vtable this renderer
	 * carries. asp_walk handles the per-root split. */
	rdr_begin_fn begin = r->line ? r->line->begin : r->tree->begin;
	rdr_report_fn report = r->line ? r->line->report : r->tree->report;
	rdr_end_fn end = r->line ? r->line->end : r->tree->end;

	begin(ctx);
	for (size_t i = 0; dirs[i]; i++)
		asp_walk(dirs[i], opts, r, ctx, &t, &errors, sp, dirs[i + 1] == NULL);
	if (!opts->noreport) /* --noreport: omit the report in every renderer (was unix-only) */
		report(ctx, &t);
	end(ctx);
	asp_statprov_destroy(sp);

	if (tot)
		*tot = t;
	return errors ? 2 : 0;
}
