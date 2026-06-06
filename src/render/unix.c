#include "render/unix.h"
#include "render/name.h"
#include "dstr.h"
#include "entry.h"
#include "util.h"

#include <errno.h>
#include <langinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* Sprint 02 ships UTF-8 + ASCII; the full charset table is Sprint 07.
 * UTF-8 vert is "│" + two U+00A0 NBSP (tree's exact bytes, .docs/audits/01 §3). */
static const struct linedraw LD_UTF8 = {
	"\342\224\202\302\240\302\240", /* │ + NBSP NBSP */
	"\342\224\234\342\224\200\342\224\200", /* ├── */
	"\342\224\224\342\224\200\342\224\200", /* └── */
};
static const struct linedraw LD_ASCII = { "|  ", "|--", "`--" };

static int is_utf8(const char *cs)
{
	return cs && (!strcasecmp(cs, "UTF-8") || !strcasecmp(cs, "utf8"));
}

static const struct linedraw *pick_linedraw(const char *charset_name)
{
	const char *cs = charset_name;
	if (!cs)
		cs = getenv("TREE_CHARSET");
	if (!cs)
		cs = nl_langinfo(CODESET);
	return is_utf8(cs) ? &LD_UTF8 : &LD_ASCII;
}

void unix_ctx_init(struct unix_ctx *u, int fd, int mb_cur_max, const char *charset_name)
{
	dstr_init(&u->out);
	u->fd = fd;
	u->mb_cur_max = mb_cur_max;
	u->ld = pick_linedraw(charset_name);
	u->last = NULL;
	u->last_cap = 0;
}

void unix_ctx_destroy(struct unix_ctx *u)
{
	dstr_free(&u->out);
	free(u->last);
}

static void flush_all(struct unix_ctx *u)
{
	size_t off = 0;
	while (off < u->out.len) {
		ssize_t w = write(u->fd, u->out.data + off, u->out.len - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			break; /* output error; nothing graceful to do mid-tree */
		}
		off += (size_t)w;
	}
	dstr_clear(&u->out);
}

static void maybe_flush(struct unix_ctx *u)
{
	if (u->out.len >= (64u * 1024u))
		flush_all(u);
}

static void ensure_last(struct unix_ctx *u, int depth)
{
	if ((size_t)depth >= u->last_cap) {
		size_t cap = u->last_cap ? u->last_cap * 2 : 64;
		while ((size_t)depth >= cap)
			cap *= 2;
		u->last = asp_xrealloc(u->last, cap);
		u->last_cap = cap;
	}
}

static void draw_indent(struct unix_ctx *u, int depth, int is_last)
{
	for (int i = 1; i < depth; i++) {
		dstr_appendz(&u->out, u->last[i] ? "   " : u->ld->vert);
		dstr_appendc(&u->out, ' ');
	}
	dstr_appendz(&u->out, is_last ? u->ld->corner : u->ld->vert_left);
	dstr_appendc(&u->out, ' ');
	ensure_last(u, depth);
	u->last[depth] = (unsigned char)(is_last ? 1 : 0);
}

/* --- renderer callbacks --- */

static void ux_begin(void *ctx)
{
	(void)ctx;
}

static void ux_root(void *ctx, const char *path, int failed)
{
	struct unix_ctx *u = ctx;
	name_print(&u->out, path, strlen(path), u->mb_cur_max);
	if (failed)
		dstr_appendz(&u->out, "  [error opening dir]");
	dstr_appendc(&u->out, '\n');
	maybe_flush(u);
}

static void ux_entry(void *ctx, const struct entry *e, const char *path,
		     int depth, int is_last)
{
	struct unix_ctx *u = ctx;
	(void)path;
	draw_indent(u, depth, is_last);
	name_print(&u->out, e->name, e->namelen, u->mb_cur_max);
	if (e->lnk) {
		dstr_appendz(&u->out, " -> ");
		name_print(&u->out, e->lnk, strlen(e->lnk), u->mb_cur_max);
	}
	maybe_flush(u);
}

static void ux_error(void *ctx, const char *msg)
{
	struct unix_ctx *u = ctx;
	dstr_appendz(&u->out, "  [");
	dstr_appendz(&u->out, msg);
	dstr_appendc(&u->out, ']');
}

static void ux_newline(void *ctx)
{
	struct unix_ctx *u = ctx;
	dstr_appendc(&u->out, '\n');
	maybe_flush(u);
}

static void ux_report(void *ctx, const struct totals *t)
{
	struct unix_ctx *u = ctx;
	char b[96];
	dstr_appendc(&u->out, '\n');
	int n = snprintf(b, sizeof b, "%lu director%s, %lu file%s\n",
			 t->dirs, t->dirs == 1 ? "y" : "ies",
			 t->files, t->files == 1 ? "" : "s");
	dstr_append(&u->out, b, (size_t)n);
}

static void ux_end(void *ctx)
{
	flush_all((struct unix_ctx *)ctx);
}

const struct renderer asp_unix_renderer = {
	ux_begin, ux_root, ux_entry, ux_error, ux_newline, ux_report, ux_end,
};
