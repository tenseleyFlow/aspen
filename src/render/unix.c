#include "render/unix.h"
#include "render/fileinfo.h"
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

void unix_ctx_init(struct unix_ctx *u, int fd, int mb_cur_max, const struct options *o)
{
	dstr_init(&u->out);
	u->fd = fd;
	u->mb_cur_max = mb_cur_max;
	u->np_flags = (o->quote ? NP_QUOTE : 0) | (o->noprint ? NP_NOPRINT : 0) |
		      (o->qmark ? NP_QMARK : 0);
	u->o = o;
	u->ld = pick_linedraw(o->charset);
	u->last = NULL;
	u->last_cap = 0;
}

/* tree's Ftype suffix: '/' dir (unless -d), '*' exec reg, '=' sock, '|' fifo. */
static char ftype_char(const struct options *o, enum asp_type t, int exe)
{
	switch (t) {
	case ASP_DIR:  return o->dirsonly ? 0 : '/';
	case ASP_SOCK: return '=';
	case ASP_FIFO: return '|';
	case ASP_REG:  return exe ? '*' : 0;
	default:       return 0;
	}
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

static void emit_info(struct unix_ctx *u, const struct asp_statinfo *st)
{
	char info[256];
	size_t n = asp_fillinfo(info, sizeof info, u->o, st);
	if (n) {
		dstr_append(&u->out, info, n);
		dstr_appendz(&u->out, "  ");
	}
}

static void ux_root(void *ctx, const char *path, int failed, const struct asp_statinfo *st)
{
	struct unix_ctx *u = ctx;
	emit_info(u, st); /* root gets the bracket too (tree) */
	name_print(&u->out, path, strlen(path), u->mb_cur_max, u->np_flags);
	if (failed)
		dstr_appendz(&u->out, "  [error opening dir]");
	else if (u->o->classify && !u->o->dirsonly)
		dstr_appendc(&u->out, '/'); /* root is a directory */
	dstr_appendc(&u->out, '\n');
	maybe_flush(u);
}

static void ux_entry(void *ctx, const struct entry *e, const char *path,
		     int depth, int is_last)
{
	struct unix_ctx *u = ctx;
	const struct options *o = u->o;

	if (o->metafirst) {
		emit_info(u, e->st);
		if (!o->noindent)
			draw_indent(u, depth, is_last);
	} else {
		if (!o->noindent)
			draw_indent(u, depth, is_last);
		emit_info(u, e->st);
	}

	if (o->fullpath)
		name_print(&u->out, path, strlen(path), u->mb_cur_max, u->np_flags);
	else
		name_print(&u->out, e->name, e->namelen, u->mb_cur_max, u->np_flags);

	if (e->lnk) {
		dstr_appendz(&u->out, " -> ");
		name_print(&u->out, e->lnk, strlen(e->lnk), u->mb_cur_max, u->np_flags);
	}

	if (o->classify) {
		char s = e->lnk
			? ftype_char(o, (enum asp_type)e->ltype, e->flags & ENT_LEXEC)
			: ftype_char(o, (enum asp_type)e->type, e->flags & ENT_EXEC);
		if (s)
			dstr_appendc(&u->out, s);
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
	int n;
	if (u->o->noreport)
		return;
	dstr_appendc(&u->out, '\n');
	if (u->o->dirsonly)
		n = snprintf(b, sizeof b, "%lu director%s\n",
			     t->dirs, t->dirs == 1 ? "y" : "ies");
	else
		n = snprintf(b, sizeof b, "%lu director%s, %lu file%s\n",
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
