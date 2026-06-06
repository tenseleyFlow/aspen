#include "render/unix.h"
#include "render/fileinfo.h"
#include "render/name.h"
#include "dstr.h"
#include "entry.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

void unix_ctx_init(struct unix_ctx *u, int fd, int mb_cur_max,
		   const struct options *o, struct colorizer *col)
{
	dstr_init(&u->out);
	u->fd = fd;
	u->mb_cur_max = mb_cur_max;
	u->np_flags = (o->quote ? NP_QUOTE : 0) | (o->noprint ? NP_NOPRINT : 0) |
		      (o->qmark ? NP_QMARK : 0);
	u->o = o;
	u->col = col;
	u->ld = asp_linedraw(o);
	u->last = NULL;
	u->last_cap = 0;

	u->hyper = o->hyperlink;
	u->pathoffset = 0;
	u->realbase[0] = '\0';
	if (u->hyper) {
		/* scheme: tree only honors --scheme when the value contains ':'
		 * (the no-colon branch is a tree bug that never assigns it), so a
		 * colonless scheme keeps the default file://. Reproduced for parity. */
		u->scheme = (o->scheme && strchr(o->scheme, ':')) ? o->scheme : "file://";
		/* authority: --authority ('.' => empty), else hostname */
		if (o->authority) {
			snprintf(u->authority, sizeof u->authority, "%s",
				 strcmp(o->authority, ".") == 0 ? "" : o->authority);
		} else if (gethostname(u->authority, sizeof u->authority) != 0) {
			u->authority[0] = '\0';
		}
		u->authority[sizeof u->authority - 1] = '\0';
	}
}

/* url_encode: whitelist alnum + "/-._~", else %XX (uppercase); returns whether
 * the last byte was '/'. Ported from tree's html.c url_encode (2.3.2). */
static int url_encode_n(struct dstr *out, const char *s, size_t n)
{
	static const char unreserved[] = "/-._~";
	int slash = 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i]; /* signed, like tree */
		if (isalnum((unsigned char)c) || strchr(unreserved, c)) {
			dstr_appendc(out, c);
		} else {
			/* tree passes the signed char to %02X, so high bytes
			 * sign-extend to %FFFFFFXX — reproduce that exactly. */
			char b[16];
			int m = snprintf(b, sizeof b, "%%%02X", c);
			if (m > 0)
				dstr_append(out, b, (size_t)m);
		}
		slash = (c == '/');
	}
	return slash;
}

/* OSC-8 open, mirroring tree's open_hyperlink(dirname, filename). Builds
 * scheme://authority:<realbase>/<dirname+offset>/<filename>. */
static void open_hyperlink(struct unix_ctx *u, const char *dirname, size_t dirnamelen,
			   const char *filename, size_t filenamelen)
{
	size_t off = u->pathoffset;
	const char *subdir = dirname + off;
	size_t subdirlen = dirnamelen > off ? dirnamelen - off : 0;

	dstr_appendz(&u->out, "\033]8;;");
	dstr_appendz(&u->out, u->scheme);
	url_encode_n(&u->out, u->authority, strlen(u->authority));
	dstr_appendc(&u->out, ':');
	int slash = url_encode_n(&u->out, u->realbase, strlen(u->realbase));
	if (subdirlen) {
		slash = slash || (subdir[0] == '/');
		if (!slash)
			dstr_appendc(&u->out, '/');
		if (!url_encode_n(&u->out, subdir, subdirlen))
			dstr_appendc(&u->out, '/');
	} else if (!slash) {
		dstr_appendc(&u->out, '/');
	}
	url_encode_n(&u->out, filename, filenamelen);
	dstr_appendz(&u->out, "\033\\");
}

static void close_hyperlink(struct unix_ctx *u)
{
	dstr_appendz(&u->out, "\033]8;;\033\\");
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

/* Indent for .info comment lines: every level uses the continuation form (the
 * comment hangs under the entry), matching tree's indent() with dirs[d+1]=1. */
static void draw_comment_indent(struct unix_ctx *u, int depth)
{
	for (int i = 1; i <= depth; i++) {
		dstr_appendz(&u->out, u->last[i] ? "   " : u->ld->vert);
		dstr_appendc(&u->out, ' ');
	}
}

/* --- renderer callbacks --- */

static void ux_begin(void *ctx)
{
	(void)ctx;
}

static void ux_comment(void *ctx, const struct entry *e, int depth)
{
	struct unix_ctx *u = ctx;
	if (!e->info)
		return;
	size_t lines = 0;
	while (e->info[lines])
		lines++;
	for (size_t ln = 0; ln < lines; ln++) {
		draw_comment_indent(u, depth);
		const char *dec;
		if (lines == 1)
			dec = u->ld->csingle;
		else if (ln == 0)
			dec = u->ld->ctop;
		else if (ln < 2)
			dec = (lines == 2) ? u->ld->cbot : u->ld->cmid;
		else
			dec = (ln == lines - 1) ? u->ld->cbot : u->ld->cext;
		dstr_appendz(&u->out, dec);
		dstr_appendc(&u->out, ' ');
		dstr_appendz(&u->out, e->info[ln]);
		dstr_appendc(&u->out, '\n');
	}
	maybe_flush(u);
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
	size_t plen = strlen(path);
	if (u->hyper) { /* per-root: resolve absolute base + offset */
		if (realpath(path, u->realbase) == NULL) {
			u->realbase[0] = '\0';
			u->pathoffset = 0;
		} else {
			u->pathoffset = plen;
		}
	}
	emit_info(u, st); /* root gets the bracket too (tree) */
	if (u->hyper && !failed)
		open_hyperlink(u, path, plen, "", 0);
	int colored = 0;
	if (!failed && u->col->enabled && st)
		colored = color_apply(u->col, &u->out, st->mode, "", 0, 0);
	name_print(&u->out, path, plen, u->mb_cur_max, u->np_flags);
	if (colored)
		color_end(u->col, &u->out);
	if (u->hyper && !failed)
		close_hyperlink(u);
	if (failed)
		dstr_appendz(&u->out, "  [error opening dir]");
	else if (u->o->classify && !u->o->dirsonly)
		dstr_appendc(&u->out, '/'); /* root is a directory (after color reset) */
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

	size_t plen = strlen(path);
	size_t dirlen = plen - e->namelen - 1; /* path minus "/name" */

	/* name: optional OSC-8 link, then color, then the name. */
	if (u->hyper)
		open_hyperlink(u, path, dirlen, e->name, e->namelen);
	int colored = 0;
	if (u->col->enabled) {
		mode_t m = (e->lnk && u->col->linktargetcolor)
			? e->lmode
			: (e->st ? e->st->mode : 0);
		colored = color_apply(u->col, &u->out, m, e->name,
				      e->flags & ENT_ORPHAN, 0);
	}
	if (o->fullpath)
		name_print(&u->out, path, plen, u->mb_cur_max, u->np_flags);
	else
		name_print(&u->out, e->name, e->namelen, u->mb_cur_max, u->np_flags);
	if (colored)
		color_end(u->col, &u->out);
	if (u->hyper)
		close_hyperlink(u);

	/* -F suffix for non-links goes after the color reset. */
	if (o->classify && !e->lnk) {
		char s = ftype_char(o, (enum asp_type)e->type, e->flags & ENT_EXEC);
		if (s)
			dstr_appendc(&u->out, s);
	}

	if (e->lnk) {
		dstr_appendz(&u->out, " -> ");
		if (u->hyper)
			open_hyperlink(u, path, dirlen, e->name, e->namelen);
		int lc = 0;
		if (u->col->enabled)
			lc = color_apply(u->col, &u->out, e->lmode, e->lnk,
					 e->flags & ENT_ORPHAN, 1);
		name_print(&u->out, e->lnk, strlen(e->lnk), u->mb_cur_max, u->np_flags);
		if (lc)
			color_end(u->col, &u->out);
		if (u->hyper)
			close_hyperlink(u);
		if (o->classify) {
			char s = ftype_char(o, (enum asp_type)e->ltype, e->flags & ENT_LEXEC);
			if (s)
				dstr_appendc(&u->out, s);
		}
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
	if (u->o->duflag) {
		char sb[64];
		asp_psize(sb, u->o, t->size); /* leading-space form, like tree */
		dstr_appendz(&u->out, sb);
		dstr_appendz(&u->out, (u->o->humanflag || u->o->siflag) ? " used in "
								       : " bytes used in ");
	}
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
	ux_begin, ux_root, ux_entry, ux_error, ux_newline, ux_comment, ux_report, ux_end,
};
