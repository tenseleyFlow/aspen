#include "render/html.h"
#include "render/escape.h"
#include "render/fileinfo.h"
#include "charset.h"
#include "dstr.h"
#include "entry.h"
#include "options.h"
#include "util.h"
#include "version.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SP "&nbsp;"

void html_ctx_init(struct html_ctx *h, int fd, int mb_cur_max, const struct options *o)
{
	dstr_init(&h->out);
	h->fd = fd;
	h->mb_cur_max = mb_cur_max;
	h->o = o;
	h->ld = asp_linedraw(o);
	h->charset = asp_charset_name(o);
	h->last = NULL;
	h->last_cap = 0;
	h->htmldirlen = 0;
}

void html_ctx_destroy(struct html_ctx *h)
{
	dstr_free(&h->out);
	free(h->last);
}

static void hflush(struct html_ctx *h)
{
	size_t off = 0;
	while (off < h->out.len) {
		ssize_t w = write(h->fd, h->out.data + off, h->out.len - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		off += (size_t)w;
	}
	dstr_clear(&h->out);
}

static void hmaybe(struct html_ctx *h)
{
	if (h->out.len >= (64u * 1024u))
		hflush(h);
}

static void cat_file(struct html_ctx *h, const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, fp)) > 0)
		dstr_append(&h->out, buf, n);
	fclose(fp);
}

static void ensure_last(struct html_ctx *h, int depth)
{
	if ((size_t)depth >= h->last_cap) {
		size_t cap = h->last_cap ? h->last_cap * 2 : 64;
		while ((size_t)depth >= cap)
			cap *= 2;
		h->last = asp_xrealloc(h->last, cap);
		h->last_cap = cap;
	}
}

/* tree's indent() for flag.H: leading tab; finished ancestors use &nbsp;×3;
 * each level separated by &nbsp;. */
static void html_indent(struct html_ctx *h, int depth, int is_last)
{
	dstr_appendc(&h->out, '\t');
	for (int i = 1; i < depth; i++) {
		dstr_appendz(&h->out, h->last[i] ? SP SP SP : h->ld->vert);
		dstr_appendz(&h->out, SP);
	}
	dstr_appendz(&h->out, is_last ? h->ld->corner : h->ld->vert_left);
	dstr_appendz(&h->out, SP);
	ensure_last(h, depth);
	h->last[depth] = (unsigned char)(is_last ? 1 : 0);
}

/* tree's html_print: spaces -> &nbsp;, then two trailing &nbsp;. */
static void html_print(struct html_ctx *h, const char *s)
{
	for (; *s; s++) {
		if (*s == ' ')
			dstr_appendz(&h->out, SP);
		else
			dstr_appendc(&h->out, *s);
	}
	dstr_appendz(&h->out, SP SP);
}

static void emit_info(struct html_ctx *h, const struct asp_statinfo *st)
{
	char info[512]; /* match tree info[512] */
	size_t n = asp_fillinfo(info, sizeof info, h->o, st);
	if (n && info[0] == '[') {
		html_print(h, info);
		dstr_appendz(&h->out, SP SP);
	}
}

static const char *cls(enum asp_type t, int isexe)
{
	if (t == ASP_DIR)
		return "DIR";
	if (isexe)
		return "EXEC";
	if (t == ASP_FIFO)
		return "FIFO";
	if (t == ASP_SOCK)
		return "SOCK";
	return "NORM";
}

/* Emit the <a ...>name</a> for an entry. dirname/filename per tree's
 * html_printfile; descend: 1 for a directory, 0 otherwise (no -R). */
static void anchor(struct html_ctx *h, const struct entry *e, const char *dirname,
		   size_t dirlen, const char *filename, int isdir, int descend)
{
	const struct options *o = h->o;
	dstr_appendz(&h->out, "<a");
	if (o->forcecolor) {
		dstr_appendz(&h->out, " class=\"");
		dstr_appendz(&h->out, cls(isdir ? ASP_DIR : (enum asp_type)e->type,
					  e ? (e->flags & ENT_EXEC) : 0));
		dstr_appendc(&h->out, '"');
	}
	if (e && e->info) {
		dstr_appendz(&h->out, " title=\"");
		for (size_t i = 0; e->info[i]; i++) {
			asp_html_encode(&h->out, e->info[i]);
			if (e->info[i + 1])
				dstr_appendc(&h->out, '\n');
		}
		dstr_appendc(&h->out, '"');
	}
	if (!o->nolinks) {
		dstr_appendz(&h->out, " href=\"");
		dstr_appendz(&h->out, o->host ? o->host : "");
		size_t off = o->htmloffset ? (dirlen >= h->htmldirlen ? h->htmldirlen : 0) : 0;
		asp_url_encode_n(&h->out, dirname + off, dirlen - off);
		/* tree compares the (truncated) dirname against filename; length-aware
		 * so -f (filename==full path) still differs from the parent dirname. */
		int same = strlen(filename) == dirlen && memcmp(dirname, filename, dirlen) == 0;
		if (!same) {
			if (dirlen == 0 || dirname[dirlen - 1] != '/')
				dstr_appendc(&h->out, '/');
			asp_url_encode(&h->out, filename);
		}
		if (isdir && descend < 2)
			dstr_appendc(&h->out, '/');
		dstr_appendc(&h->out, '"');
	}
	dstr_appendc(&h->out, '>');
	asp_html_encode(&h->out, filename);
	dstr_appendz(&h->out, "</a>");
}

/* --- renderer callbacks --- */

static void html_begin(void *ctx)
{
	struct html_ctx *h = ctx;
	const struct options *o = h->o;
	if (o->hintro) {
		cat_file(h, o->hintro);
		return;
	}
	const char *title = o->title ? o->title : "Directory Tree";
	/* Build straight into the output buffer — a fixed stack buffer (the old
	 * char[1024]) silently truncated the document when -T title or the charset
	 * was long. Variable parts are appended separately; the rest is one literal. */
	struct dstr *d = &h->out;
	dstr_appendz(d, "<!DOCTYPE html>\n<html>\n<head>\n"
			" <meta http-equiv=\"Content-Type\" content=\"text/html; charset=");
	dstr_appendz(d, h->charset ? h->charset : "iso-8859-1");
	dstr_appendz(d, "\">\n"
			" <meta name=\"Author\" content=\"Made by '" ASP_PROGNAME "'\">\n"
			" <meta name=\"GENERATOR\" content=\"" ASP_PROGNAME " v" ASP_VERSION "\">\n"
			" <title>");
	dstr_appendz(d, title);
	dstr_appendz(d, "</title>\n"
			" <style type=\"text/css\">\n"
			"  BODY { font-family : monospace, sans-serif;  color: black;}\n"
			"  P { font-family : monospace, sans-serif; color: black; margin:0px; padding: 0px;}\n"
			"  A:visited { text-decoration : none; margin : 0px; padding : 0px;}\n"
			"  A:link    { text-decoration : none; margin : 0px; padding : 0px;}\n"
			"  A:hover   { text-decoration: underline; background-color : yellow; margin : 0px; padding : 0px;}\n"
			"  A:active  { margin : 0px; padding : 0px;}\n"
			"  .VERSION { font-size: small; font-family : arial, sans-serif; }\n"
			"  .NORM  { color: black;  }\n"
			"  .FIFO  { color: purple; }\n"
			"  .CHAR  { color: yellow; }\n"
			"  .DIR   { color: blue;   }\n"
			"  .BLOCK { color: yellow; }\n"
			"  .LINK  { color: aqua;   }\n"
			"  .SOCK  { color: fuchsia;}\n"
			"  .EXEC  { color: green;  }\n"
			" </style>\n</head>\n<body>\n\t<h1>");
	dstr_appendz(d, title);
	dstr_appendz(d, "</h1><p>\n");
}

static void html_end(void *ctx)
{
	struct html_ctx *h = ctx;
	if (h->o->houtro) {
		cat_file(h, h->o->houtro);
	} else {
		dstr_appendz(&h->out, "\t<hr>\n\t<p class=\"VERSION\">\n\t\t");
		dstr_appendz(&h->out, ASP_PROGNAME " v" ASP_VERSION "\n");
		dstr_appendz(&h->out, "\t</p>\n</body>\n</html>\n");
	}
	hflush(h);
}

static void html_root(void *ctx, const char *path, int failed, const struct asp_statinfo *st)
{
	struct html_ctx *h = ctx;
	h->htmldirlen = strlen(path);
	dstr_appendc(&h->out, '\t'); /* root line is indented like tree's html */
	emit_info(h, st);
	/* tree emits the anchor's href for any root that EXISTS — including the
	 * fifo/exec/symlink/unreadable roots whose opendir fails — and a bare <a>
	 * only for a root that doesn't stat at all (nonexistent). aspen knows the
	 * root exists when opendir succeeded (!failed) or its lstat did (st); a stat
	 * is computed lazily, so st==NULL with !failed is an opened-fine directory.
	 * The href's trailing '/' tracks isdir (a non-dir/file root gets none). */
	if (!failed || st) {
		int isdir = st ? S_ISDIR(st->mode) : 1;
		anchor(h, NULL, path, strlen(path), path, isdir, 1);
	} else {
		dstr_appendz(&h->out, "<a>");
		asp_html_encode(&h->out, path);
		dstr_appendz(&h->out, "</a>");
	}
	if (failed)
		dstr_appendz(&h->out, "  [error opening dir]");
	dstr_appendz(&h->out, "<br>\n"); /* root self-terminates, like ux_root's '\n' */
	hmaybe(h);
}

static void html_entry(void *ctx, const struct entry *e, const char *path, int depth, int is_last)
{
	struct html_ctx *h = ctx;
	const struct options *o = h->o;
	int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);

	if (o->metafirst) {
		emit_info(h, e->st);
		if (!o->noindent)
			html_indent(h, depth, is_last);
	} else {
		if (!o->noindent)
			html_indent(h, depth, is_last);
		emit_info(h, e->st);
	}

	size_t plen = strlen(path);
	size_t dirlen = plen - e->namelen - 1;
	const char *filename = o->fullpath ? path : e->name;
	int descend = dir_like ? 1 : 0;
	anchor(h, e, path, dirlen, filename, dir_like, descend);
	hmaybe(h);
}

static void html_error(void *ctx, const char *msg)
{
	struct html_ctx *h = ctx;
	dstr_appendz(&h->out, "  [");
	dstr_appendz(&h->out, msg);
	dstr_appendc(&h->out, ']');
}

static void html_newline(void *ctx)
{
	struct html_ctx *h = ctx;
	dstr_appendz(&h->out, "<br>\n");
	hmaybe(h);
}

static void html_comment(void *ctx, const struct entry *e, int depth)
{
	(void)ctx; (void)e; (void)depth; /* HTML puts .info in the <a title="..."> */
}

static void html_report(void *ctx, const struct totals *t)
{
	struct html_ctx *h = ctx;
	char b[160];
	dstr_appendz(&h->out, "<br><br><p>\n\n");
	if (h->o->duflag) {
		char sb[64];
		asp_psize(sb, sizeof sb, h->o, t->size);
		dstr_appendz(&h->out, sb);
		dstr_appendz(&h->out, (h->o->humanflag || h->o->siflag) ? " used in " : " bytes used in ");
	}
	if (h->o->dirsonly)
		snprintf(b, sizeof b, "%lu director%s\n", t->dirs, t->dirs == 1 ? "y" : "ies");
	else
		snprintf(b, sizeof b, "%lu director%s, %lu file%s\n", t->dirs,
			 t->dirs == 1 ? "y" : "ies", t->files, t->files == 1 ? "" : "s");
	dstr_appendz(&h->out, b);
	dstr_appendz(&h->out, "\n</p>\n");
}

const struct renderer asp_html_renderer = {
	html_begin, html_root, html_entry, html_error, html_newline, html_comment,
	html_report, html_end, NULL,
};
