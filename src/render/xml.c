#include "render/xml.h"
#include "render/escape.h"
#include "render/fileinfo.h"
#include "charset.h"
#include "dstr.h"
#include "entry.h"
#include "idcache.h"
#include "options.h"
#include "sort.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void xml_ctx_init(struct xml_ctx *x, int fd, const struct options *o)
{
	dstr_init(&x->out);
	x->fd = fd;
	x->o = o;
}

void xml_ctx_destroy(struct xml_ctx *x)
{
	dstr_free(&x->out);
}

static const char *xnl(struct xml_ctx *x)
{
	return x->o->noindent ? "" : "\n";
}

static void xflush(struct xml_ctx *x)
{
	size_t off = 0;
	while (off < x->out.len) {
		ssize_t w = write(x->fd, x->out.data + off, x->out.len - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		off += (size_t)w;
	}
	dstr_clear(&x->out);
}

static void xmaybe(struct xml_ctx *x)
{
	if (x->out.len >= (64u * 1024u))
		xflush(x);
}

static void xindent(struct xml_ctx *x, int level)
{
	if (x->o->noindent)
		return;
	for (int i = 0; i <= level; i++)
		dstr_appendz(&x->out, "    ");
}

static const char *tag_str(enum asp_type t)
{
	switch (t) {
	case ASP_DIR:  return "directory";
	case ASP_REG:  return "file";
	case ASP_LNK:  return "link";
	case ASP_CHR:  return "char";
	case ASP_BLK:  return "block";
	case ASP_SOCK: return "socket";
	case ASP_FIFO: return "fifo";
	default:       return "unknown";
	}
}

static void xfillinfo(struct xml_ctx *x, const struct asp_statinfo *st)
{
	const struct options *o = x->o;
	char b[256];
	if (!st)
		return;
	if (o->inodeflag) {
		snprintf(b, sizeof b, " inode=\"%lld\"", (long long)st->ino);
		dstr_appendz(&x->out, b);
	}
	if (o->devflag) {
		snprintf(b, sizeof b, " dev=\"%d\"", (int)st->dev);
		dstr_appendz(&x->out, b);
	}
	if (o->permflag) {
		snprintf(b, sizeof b, " mode=\"%04o\" prot=\"%s\"",
			 (unsigned)(st->mode & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX)),
			 asp_prot(st->mode));
		dstr_appendz(&x->out, b);
	}
	if (o->userflag) {
		snprintf(b, sizeof b, " user=\"%s\"", uidtoname(st->uid));
		dstr_appendz(&x->out, b);
	}
	if (o->groupflag) {
		snprintf(b, sizeof b, " group=\"%s\"", gidtoname(st->gid));
		dstr_appendz(&x->out, b);
	}
	if (o->sizeflag) {
		snprintf(b, sizeof b, " size=\"%lld\"", (long long)st->size);
		dstr_appendz(&x->out, b);
	}
	if (o->dateflag) {
		snprintf(b, sizeof b, " time=\"%s\"",
			 asp_do_date(o, o->ctimeflag ? st->ctime : st->mtime));
		dstr_appendz(&x->out, b);
	}
}

/* Open tag: <tag name="..." [info=][target=][attrs]> */
static void xhead(struct xml_ctx *x, enum asp_type type, const char *name,
		  const struct entry *e, const struct asp_statinfo *st)
{
	dstr_appendc(&x->out, '<');
	dstr_appendz(&x->out, tag_str(type));
	dstr_appendz(&x->out, " name=\"");
	asp_html_encode(&x->out, name);
	dstr_appendc(&x->out, '"');
	if (e && e->info) {
		dstr_appendz(&x->out, " info=\"");
		for (size_t i = 0; e->info[i]; i++) {
			asp_html_encode(&x->out, e->info[i]);
			if (e->info[i + 1])
				dstr_appendz(&x->out, xnl(x));
		}
		dstr_appendc(&x->out, '"');
	}
	if (e && e->lnk) {
		dstr_appendz(&x->out, " target=\"");
		asp_html_encode(&x->out, e->lnk);
		dstr_appendc(&x->out, '"');
	}
	xfillinfo(x, st);
	dstr_appendc(&x->out, '>');
}

static void xemit_level(struct xml_ctx *x, struct entry **arr, int depth, struct totals *tot)
{
	size_t n = 0;
	while (arr[n])
		n++;
	asp_sort(arr, n, x->o);

	for (size_t i = 0; arr[i]; i++) {
		struct entry *e = arr[i];
		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like)
			tot->dirs++;
		else
			tot->files++;

		const char *tag = tag_str(e->type);
		int has_kids = e->child && e->child[0];
		int direrr = dir_like && e->err && !has_kids;

		xindent(x, depth);
		xhead(x, e->type, e->name, e, e->st);

		if (has_kids) {
			dstr_appendz(&x->out, xnl(x));
			xemit_level(x, e->child, depth + 1, tot);
			xindent(x, depth);
			dstr_appendc(&x->out, '<');
			dstr_appendc(&x->out, '/');
			dstr_appendz(&x->out, tag);
			dstr_appendc(&x->out, '>');
			dstr_appendz(&x->out, xnl(x));
		} else {
			if (direrr) {
				dstr_appendz(&x->out, "<error>");
				dstr_appendz(&x->out, e->err);
				dstr_appendz(&x->out, "</error>");
			}
			dstr_appendz(&x->out, "</");
			dstr_appendz(&x->out, tag);
			dstr_appendc(&x->out, '>');
			dstr_appendz(&x->out, xnl(x));
		}
		xmaybe(x);
	}
}

/* --- renderer callbacks --- */

static void xml_begin(void *ctx)
{
	struct xml_ctx *x = ctx;
	const char *cs = asp_charset_name(x->o);
	dstr_appendz(&x->out, "<?xml version=\"1.0\"");
	if (cs) {
		dstr_appendz(&x->out, " encoding=\"");
		dstr_appendz(&x->out, cs);
		dstr_appendc(&x->out, '"');
	}
	dstr_appendz(&x->out, "?>");
	dstr_appendz(&x->out, xnl(x));
	dstr_appendz(&x->out, "<tree>");
	dstr_appendz(&x->out, xnl(x));
}

static void xml_end(void *ctx)
{
	struct xml_ctx *x = ctx;
	dstr_appendz(&x->out, "</tree>");
	dstr_appendz(&x->out, xnl(x));
	xflush(x);
}

static void xml_report(void *ctx, const struct totals *t)
{
	struct xml_ctx *x = ctx;
	char b[128];
	const char *nl = xnl(x);
	xindent(x, 0);
	dstr_appendz(&x->out, "<report>");
	dstr_appendz(&x->out, nl);
	if (x->o->duflag) {
		xindent(x, 1);
		snprintf(b, sizeof b, "<size>%lld</size>", (long long)t->size);
		dstr_appendz(&x->out, b);
		dstr_appendz(&x->out, nl);
	}
	xindent(x, 1);
	snprintf(b, sizeof b, "<directories>%lu</directories>", t->dirs);
	dstr_appendz(&x->out, b);
	dstr_appendz(&x->out, nl);
	if (!x->o->dirsonly) {
		xindent(x, 1);
		snprintf(b, sizeof b, "<files>%lu</files>", t->files);
		dstr_appendz(&x->out, b);
		dstr_appendz(&x->out, nl);
	}
	xindent(x, 0);
	dstr_appendz(&x->out, "</report>");
	dstr_appendz(&x->out, nl);
}

static void xml_tree(void *ctx, const char *rootpath, const struct asp_statinfo *st,
		     int opened, struct entry **top, struct totals *tot, int last_root)
{
	struct xml_ctx *x = ctx;
	(void)last_root;

	xindent(x, 0);
	if (!opened) { /* failed root: descend is forced, so a newline precedes close */
		dstr_appendz(&x->out, "<unknown name=\"");
		asp_html_encode(&x->out, rootpath);
		dstr_appendz(&x->out, "\"><error>error opening dir</error>");
		dstr_appendz(&x->out, xnl(x));
		xindent(x, 0);
		dstr_appendz(&x->out, "</unknown>");
		dstr_appendz(&x->out, xnl(x));
		return;
	}

	dstr_appendz(&x->out, "<directory name=\"");
	asp_html_encode(&x->out, rootpath);
	dstr_appendc(&x->out, '"');
	xfillinfo(x, st);
	dstr_appendc(&x->out, '>');
	dstr_appendz(&x->out, xnl(x));
	tot->dirs++;

	xemit_level(x, top, 1, tot);

	xindent(x, 0);
	dstr_appendz(&x->out, "</directory>");
	dstr_appendz(&x->out, xnl(x));
	xmaybe(x);
}

const struct renderer asp_xml_renderer = {
	xml_begin, NULL, NULL, NULL, NULL, NULL, xml_report, xml_end, xml_tree,
};
