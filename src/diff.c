#include "diff.h"
#include "arena.h"
#include "charset.h"
#include "color.h"
#include "dstr.h"
#include "options.h"
#include "render/name.h"
#include "render/outbuf.h"
#include "traverse.h"
#include "util.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct diff_ctx {
	struct dstr out;
	int fd;
	int mb_cur_max;
	int np_flags;
	int use_color;
	const struct linedraw *ld;
	struct colorizer *col;
	const char *ind_vert, *ind_vleft, *ind_corner, *ind_space;
	size_t ind_vert_n, ind_vleft_n, ind_corner_n, ind_space_n;
	unsigned char *last;
	size_t last_cap;
	struct diff_totals tot;
};

static void diff_ctx_init(struct diff_ctx *d, int fd, int mb,
			   const struct options *o, struct colorizer *col)
{
	dstr_init(&d->out);
	dstr_reserve(&d->out, ASP_OUT_FLUSH);
	d->fd = fd;
	d->mb_cur_max = mb;
	d->np_flags = (o->quote ? NP_QUOTE : 0) | (o->noprint ? NP_NOPRINT : 0) |
		      (o->qmark ? NP_QMARK : 0);
	d->use_color = col->enabled;
	d->ld = asp_linedraw(o);
	d->col = col;
	int clvl = o->compress_indent;
	static const char *const spaces[3] = { "   ", "  ", " " };
	d->ind_vert = d->ld->vert[clvl];       d->ind_vert_n = strlen(d->ind_vert);
	d->ind_vleft = d->ld->vert_left[clvl]; d->ind_vleft_n = strlen(d->ind_vleft);
	d->ind_corner = d->ld->corner[clvl];   d->ind_corner_n = strlen(d->ind_corner);
	d->ind_space = spaces[clvl];            d->ind_space_n = strlen(d->ind_space);
	d->last = NULL;
	d->last_cap = 0;
	d->tot = (struct diff_totals){ 0, 0, 0, 0 };
}

static void diff_ctx_free(struct diff_ctx *d)
{
	dstr_free(&d->out);
	free(d->last);
}

/* --- merge helpers --- */

static int is_dir_like(const struct entry *e)
{
	return e->type == ASP_DIR ||
	       (e->type == ASP_LNK && e->ltype == ASP_DIR);
}

static int entries_differ(const struct entry *a, const struct entry *b)
{
	if (a->type != b->type)
		return 1;
	if (a->st && b->st) {
		if (a->st->size != b->st->size)
			return 1;
		if (a->st->mtime != b->st->mtime)
			return 1;
		if (a->st->mode != b->st->mode)
			return 1;
	} else if (a->st != b->st) {
		return 1;
	}
	if (a->lnk && b->lnk)
		return strcmp(a->lnk, b->lnk) != 0;
	if (a->lnk != b->lnk)
		return 1;
	return 0;
}

static int subtree_has_changes(struct diff_entry **ch)
{
	if (!ch)
		return 0;
	for (size_t i = 0; ch[i]; i++) {
		if (ch[i]->status != DIFF_UNCHANGED)
			return 1;
		if (subtree_has_changes(ch[i]->child))
			return 1;
	}
	return 0;
}

static size_t arr_len(struct entry **a)
{
	if (!a)
		return 0;
	size_t n = 0;
	while (a[n])
		n++;
	return n;
}

static struct diff_entry *de_new(struct arena *a, enum diff_status s,
				 const struct entry *ea, const struct entry *eb)
{
	struct diff_entry *d = arena_alloc(a, sizeof *d);
	d->status = s;
	d->a = ea;
	d->b = eb;
	d->child = NULL;
	return d;
}

static struct diff_entry **annotate_subtree(struct entry **arr,
					    enum diff_status status,
					    struct arena *out);

static struct diff_entry **merge_level(struct entry **a_arr, struct entry **b_arr,
				       struct arena *out)
{
	size_t na = arr_len(a_arr);
	size_t nb = arr_len(b_arr);
	size_t cap = na + nb + 1;
	struct diff_entry **res = arena_alloc(out, cap * sizeof *res);
	size_t ri = 0;
	size_t ia = 0, ib = 0;

	while (ia < na && ib < nb) {
		int cmp = strcoll(a_arr[ia]->name, b_arr[ib]->name);
		if (cmp < 0) {
			struct diff_entry *d = de_new(out, DIFF_REMOVED, a_arr[ia], NULL);
			d->child = annotate_subtree(a_arr[ia]->child, DIFF_REMOVED, out);
			res[ri++] = d;
			ia++;
		} else if (cmp > 0) {
			struct diff_entry *d = de_new(out, DIFF_ADDED, NULL, b_arr[ib]);
			d->child = annotate_subtree(b_arr[ib]->child, DIFF_ADDED, out);
			res[ri++] = d;
			ib++;
		} else {
			int a_dir = is_dir_like(a_arr[ia]);
			int b_dir = is_dir_like(b_arr[ib]);
			struct diff_entry *d = de_new(out, DIFF_UNCHANGED,
						      a_arr[ia], b_arr[ib]);
			if (a_dir && b_dir) {
				d->child = merge_level(a_arr[ia]->child,
						       b_arr[ib]->child, out);
				if (entries_differ(a_arr[ia], b_arr[ib]))
					d->status = DIFF_MODIFIED;
			} else if (a_dir != b_dir) {
				d->status = DIFF_MODIFIED;
				if (b_dir && b_arr[ib]->child)
					d->child = annotate_subtree(b_arr[ib]->child,
								    DIFF_ADDED, out);
				else if (a_dir && a_arr[ia]->child)
					d->child = annotate_subtree(a_arr[ia]->child,
								    DIFF_REMOVED, out);
			} else {
				if (entries_differ(a_arr[ia], b_arr[ib]))
					d->status = DIFF_MODIFIED;
			}
			res[ri++] = d;
			ia++;
			ib++;
		}
	}
	while (ia < na) {
		struct diff_entry *d = de_new(out, DIFF_REMOVED, a_arr[ia], NULL);
		d->child = annotate_subtree(a_arr[ia]->child, DIFF_REMOVED, out);
		res[ri++] = d;
		ia++;
	}
	while (ib < nb) {
		struct diff_entry *d = de_new(out, DIFF_ADDED, NULL, b_arr[ib]);
		d->child = annotate_subtree(b_arr[ib]->child, DIFF_ADDED, out);
		res[ri++] = d;
		ib++;
	}
	res[ri] = NULL;
	return res;
}

static struct diff_entry **annotate_subtree(struct entry **arr,
					    enum diff_status status,
					    struct arena *out)
{
	size_t n = arr_len(arr);
	if (n == 0)
		return NULL;
	struct diff_entry **res = arena_alloc(out, (n + 1) * sizeof *res);
	for (size_t i = 0; i < n; i++) {
		const struct entry *ea = (status == DIFF_REMOVED) ? arr[i] : NULL;
		const struct entry *eb = (status == DIFF_ADDED)   ? arr[i] : NULL;
		res[i] = de_new(out, status, ea, eb);
		res[i]->child = annotate_subtree(arr[i]->child, status, out);
	}
	res[n] = NULL;
	return res;
}

/* --- output --- */

static int should_show(const struct diff_entry *d)
{
	if (d->status != DIFF_UNCHANGED)
		return 1;
	return subtree_has_changes(d->child);
}

static size_t count_visible(struct diff_entry **arr)
{
	if (!arr)
		return 0;
	size_t n = 0;
	for (size_t i = 0; arr[i]; i++)
		if (should_show(arr[i]))
			n++;
	return n;
}

static void ensure_last(struct diff_ctx *d, int depth)
{
	if ((size_t)depth >= d->last_cap) {
		size_t cap = d->last_cap ? d->last_cap * 2 : 64;
		while ((size_t)depth >= cap)
			cap *= 2;
		d->last = asp_xrealloc(d->last, cap);
		d->last_cap = cap;
	}
}

static void draw_indent(struct diff_ctx *d, int depth, int is_last)
{
	for (int i = 1; i < depth; i++) {
		if (d->last[i])
			dstr_append(&d->out, d->ind_space, d->ind_space_n);
		else
			dstr_append(&d->out, d->ind_vert, d->ind_vert_n);
		dstr_appendc(&d->out, ' ');
	}
	if (depth > 0) {
		if (is_last)
			dstr_append(&d->out, d->ind_corner, d->ind_corner_n);
		else
			dstr_append(&d->out, d->ind_vleft, d->ind_vleft_n);
		dstr_appendc(&d->out, ' ');
	}
	ensure_last(d, depth);
	d->last[depth] = (unsigned char)(is_last ? 1 : 0);
}

static void emit_marker(struct diff_ctx *d, enum diff_status s)
{
	const char *color = NULL;
	const char *label = NULL;
	switch (s) {
	case DIFF_ADDED:    color = "\033[32m"; label = "[+] "; break;
	case DIFF_REMOVED:  color = "\033[31m"; label = "[-] "; break;
	case DIFF_MODIFIED: color = "\033[33m"; label = "[~] "; break;
	case DIFF_UNCHANGED: break;
	}
	if (label) {
		if (d->use_color && color)
			dstr_appendz(&d->out, color);
		dstr_appendz(&d->out, label);
		if (d->use_color)
			dstr_appendz(&d->out, "\033[0m");
	} else {
		dstr_appendz(&d->out, "    ");
	}
}

static void diff_emit_level(struct diff_ctx *d, struct diff_entry **arr, int depth)
{
	if (!arr)
		return;
	size_t vis = count_visible(arr);
	size_t vi = 0;
	for (size_t i = 0; arr[i]; i++) {
		struct diff_entry *de = arr[i];
		if (!should_show(de))
			continue;
		vi++;
		int is_last = (vi == vis);
		draw_indent(d, depth, is_last);
		emit_marker(d, de->status);

		const struct entry *e = de->b ? de->b : de->a;
		int colored = 0;
		if (d->col->enabled && e->st)
			colored = color_apply(d->col, &d->out, e->st->mode,
					      e->name, e->flags & ENT_ORPHAN, 0);
		name_print(&d->out, e->name, e->namelen, d->mb_cur_max, d->np_flags);
		if (colored)
			color_end(d->col, &d->out);

		if (e->lnk) {
			dstr_appendz(&d->out, " -> ");
			name_print(&d->out, e->lnk, strlen(e->lnk),
				   d->mb_cur_max, d->np_flags);
		}
		dstr_appendc(&d->out, '\n');
		asp_out_maybe(&d->out, d->fd);

		switch (de->status) {
		case DIFF_ADDED:    d->tot.added++; break;
		case DIFF_REMOVED:  d->tot.removed++; break;
		case DIFF_MODIFIED: d->tot.modified++; break;
		case DIFF_UNCHANGED: d->tot.unchanged++; break;
		}

		diff_emit_level(d, de->child, depth + 1);
	}
}

int asp_diff_run(const char *dir_a, const char *dir_b,
		 const struct options *o, int outfd, int mb)
{
	struct options oc = *o;
	oc.sizeflag = 1;
	oc.sort = SORT_NAME;
	oc.reverse = 0;
	oc.dirsfirst = 0;
	oc.filesfirst = 0;

	struct statprov *sp = asp_statprov_create(&oc);

	struct asp_built_tree ta, tb;
	int fail = 0;
	if (asp_build_tree(dir_a, &oc, sp, &ta) != 0) {
		fprintf(stderr, "aspen: cannot open '%s'\n", dir_a);
		fail = 1;
	}
	if (asp_build_tree(dir_b, &oc, sp, &tb) != 0) {
		fprintf(stderr, "aspen: cannot open '%s'\n", dir_b);
		fail = 1;
	}
	asp_statprov_destroy(sp);
	if (fail)
		return 2;

	struct arena merge;
	arena_init(&merge, 0);
	struct diff_entry **top = merge_level(ta.top, tb.top, &merge);

	struct colorizer col;
	color_init(&col, &oc, outfd);
	struct diff_ctx d;
	diff_ctx_init(&d, outfd, mb, &oc, &col);

	dstr_appendz(&d.out, dir_a);
	dstr_appendz(&d.out, " -> ");
	dstr_appendz(&d.out, dir_b);
	dstr_appendc(&d.out, '\n');

	diff_emit_level(&d, top, 1);

	if (!o->noreport) {
		char buf[256];
		int n = snprintf(buf, sizeof buf,
				 "\n%lu added, %lu removed, %lu modified\n",
				 d.tot.added, d.tot.removed, d.tot.modified);
		if (n > 0)
			dstr_append(&d.out, buf, (size_t)n);
	}

	asp_out_flush(&d.out, d.fd);
	diff_ctx_free(&d);
	color_free(&col);
	arena_destroy(&merge);
	arena_destroy(&ta.arena);
	arena_destroy(&tb.arena);
	return 0;
}
