#include "render/json.h"
#include "render/fileinfo.h"
#include "render/outbuf.h"
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

void json_ctx_init(struct json_ctx *j, int fd, const struct options *o)
{
	dstr_init(&j->out);
	j->fd = fd;
	j->o = o;
	dstr_init(&j->fp);
}

void json_ctx_destroy(struct json_ctx *j)
{
	dstr_free(&j->out);
	dstr_free(&j->fp);
}

static const char *jnl(struct json_ctx *j)
{
	return j->o->noindent ? "" : "\n";
}

static void jflush(struct json_ctx *j) { asp_out_flush(&j->out, j->fd); }
static void jmaybe(struct json_ctx *j) { asp_out_maybe(&j->out, j->fd); }

/* RFC-8259 escaping (tree's json_encode; UTF-8 passes through byte-wise). */
static void jenc(struct dstr *o, const char *s)
{
	static const char ctrl[] = "0-------btn-fr------------------";
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c < 32) {
			if (ctrl[c] != '-') {
				char b[2] = { '\\', ctrl[c] };
				dstr_append(o, b, 2);
			} else {
				char b[8];
				int n = snprintf(b, sizeof b, "\\u%04x", c);
				dstr_append(o, b, (size_t)n);
			}
		} else if (c == '"' || c == '\\') {
			char b[2] = { '\\', (char)c };
			dstr_append(o, b, 2);
		} else {
			dstr_appendc(o, (char)c);
		}
	}
}

/* One indent unit, narrowed by --compress (tree's spaces[clvl], clvl = level +
 * remove_space); the default ("    ") is index 0. */
static const char *junit(struct json_ctx *j)
{
	static const char *spaces[] = { "    ", "   ", "  ", " ", "" };
	return spaces[j->o->compress_indent + j->o->remove_space];
}

static void jindent(struct json_ctx *j, int level)
{
	asp_out_indent4(&j->out, level, j->o->noindent, junit(j));
}

static void jfillinfo(struct json_ctx *j, const struct asp_statinfo *st)
{
	const struct options *o = j->o;
	char b[256];
	if (!st)
		return;
	if (o->inodeflag) {
		snprintf(b, sizeof b, ",\"inode\":%lld", (long long)st->ino);
		dstr_appendz(&j->out, b);
	}
	if (o->devflag) {
		snprintf(b, sizeof b, ",\"dev\":%d", (int)st->dev);
		dstr_appendz(&j->out, b);
	}
	if (o->permflag) {
		snprintf(b, sizeof b, ",\"mode\":\"%04o\",\"prot\":\"%s\"",
			 (unsigned)(st->mode & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX)),
			 asp_prot(st->mode));
		dstr_appendz(&j->out, b);
	}
	if (o->userflag) {
		snprintf(b, sizeof b, ",\"user\":\"%s\"", uidtoname(st->uid));
		dstr_appendz(&j->out, b);
	}
	if (o->groupflag) {
		snprintf(b, sizeof b, ",\"group\":\"%s\"", gidtoname(st->gid));
		dstr_appendz(&j->out, b);
	}
	if (o->sizeflag) {
		if (o->humanflag || o->siflag) {
			char nb[64];
			asp_psize(nb, sizeof nb, o, st->size);
			char *p = nb;
			while (*p == ' ')
				p++;
			snprintf(b, sizeof b, ",\"size\":\"%s\"", p);
		} else {
			snprintf(b, sizeof b, ",\"size\":%lld", (long long)st->size);
		}
		dstr_appendz(&j->out, b);
	}
	if (o->dateflag) {
		snprintf(b, sizeof b, ",\"time\":\"%s\"",
			 asp_do_date(o, o->ctimeflag ? st->ctime : st->mtime));
		dstr_appendz(&j->out, b);
	}
}

static void jhead(struct json_ctx *j, enum asp_type type, const char *name,
		  const struct entry *e, const struct asp_statinfo *st)
{
	dstr_appendz(&j->out, "{\"type\":\"");
	dstr_appendz(&j->out, asp_type_name(type));
	dstr_appendz(&j->out, "\",\"name\":\"");
	jenc(&j->out, name);
	dstr_appendc(&j->out, '"');
	if (e && e->info) {
		dstr_appendz(&j->out, ",\"info\":\"");
		for (size_t i = 0; e->info[i]; i++) {
			jenc(&j->out, e->info[i]);
			if (e->info[i + 1])
				dstr_appendz(&j->out, "\\n");
		}
		dstr_appendc(&j->out, '"');
	}
	if (e && e->lnk) {
		dstr_appendz(&j->out, ",\"target\":\"");
		jenc(&j->out, e->lnk);
		dstr_appendc(&j->out, '"');
	}
	jfillinfo(j, st);
}

static void jemit_level(struct json_ctx *j, struct entry **arr, int depth, struct totals *tot)
{
	size_t n = 0;
	while (arr[n])
		n++;
	asp_sort(arr, n, j->o);
	for (size_t i = 0; arr[i]; i++) {
		struct entry *e = arr[i];
		int last = (arr[i + 1] == NULL);
		int dir_like = e->type == ASP_DIR || (e->type == ASP_LNK && e->ltype == ASP_DIR);
		if (dir_like)
			tot->dirs += 1 + e->condensed; /* --condense: absorbed dirs count too */
		else
			tot->files++;

		/* --condense: emit the collapsed "a/b/c" in place of the name (and on the
		 * -f path stack). -f: name is the full path. j->fp is a path stack seeded
		 * with the root in json_tree; push "/name" before emitting, pop after. */
		const char *dname = e->condensed_name ? e->condensed_name : e->name;
		size_t dnamelen = e->condensed_name ? strlen(e->condensed_name) : e->namelen;
		size_t fp_saved = j->fp.len;
		const char *name = dname;
		if (j->o->fullpath) {
			dstr_appendc(&j->fp, '/');
			dstr_append(&j->fp, dname, dnamelen);
			name = j->fp.data;
		}

		jindent(j, depth);
		jhead(j, e->type, name, e, e->st);

		/* tree emits "contents" only for a NON-empty descended dir (an empty
		 * dir reads as no children -> no contents key) or an unreadable dir. */
		int has_kids = e->child && e->child[0];
		int direrr = dir_like && e->err && !has_kids;
		if (has_kids) {
			dstr_appendz(&j->out, ",\"contents\":[");
			dstr_appendz(&j->out, jnl(j));
			jemit_level(j, e->child, depth + 1, tot);
			jindent(j, depth);
			dstr_appendz(&j->out, "]}");
			dstr_appendz(&j->out, last ? "" : ",");
			dstr_appendz(&j->out, jnl(j));
		} else if (direrr) {
			/* unreadable / filelimit dir: tree emits the error inline and closes
			 * with json_indent(-1) == 4 spaces (no newline) — list.c close(lev=-1). */
			dstr_appendz(&j->out, ",\"contents\":[{\"error\": \"");
			dstr_appendz(&j->out, e->err);
			dstr_appendz(&j->out, "\"}");
			if (!j->o->noindent)
				dstr_appendz(&j->out, junit(j)); /* tree's json_indent(-1) */
			dstr_appendz(&j->out, "]}");
			dstr_appendz(&j->out, last ? "" : ",");
			dstr_appendz(&j->out, jnl(j));
			/* DEVIATION D2: tree, once any error occurs, gives EVERY later entry
			 * a spurious empty "contents":[    ] (its flag.J && errors quirk),
			 * producing misleading JSON. aspen does not — see .docs/deviations.md. */
		} else {
			dstr_appendc(&j->out, '}');
			dstr_appendz(&j->out, last ? "" : ",");
			dstr_appendz(&j->out, jnl(j));
		}
		if (j->o->fullpath) { /* pop this entry off the path stack */
			j->fp.len = fp_saved;
			j->fp.data[fp_saved] = '\0';
		}
		jmaybe(j);
	}
}

/* --- renderer callbacks --- */

static void json_begin(void *ctx)
{
	struct json_ctx *j = ctx;
	dstr_appendc(&j->out, '[');
	dstr_appendz(&j->out, jnl(j));
}

static void json_end(void *ctx)
{
	struct json_ctx *j = ctx;
	dstr_appendz(&j->out, jnl(j));
	dstr_appendz(&j->out, "]\n");
	jflush(j);
}

static void json_report(void *ctx, const struct totals *t)
{
	struct json_ctx *j = ctx;
	char b[128];
	dstr_appendc(&j->out, ',');
	jindent(j, 0);
	dstr_appendz(&j->out, "{\"type\":\"report\"");
	if (j->o->duflag) {
		snprintf(b, sizeof b, ",\"size\":%lld", (long long)t->size);
		dstr_appendz(&j->out, b);
	}
	snprintf(b, sizeof b, ",\"directories\":%lu", t->dirs);
	dstr_appendz(&j->out, b);
	if (!j->o->dirsonly) {
		snprintf(b, sizeof b, ",\"files\":%lu", t->files);
		dstr_appendz(&j->out, b);
	}
	dstr_appendc(&j->out, '}');
}

static void json_tree(void *ctx, const char *rootpath, const struct asp_statinfo *st,
		      int opened, const char *limit_err, struct entry **top,
		      struct totals *tot, int last_root)
{
	struct json_ctx *j = ctx;

	jindent(j, 0);
	if (!opened) { /* failed root: type from lstat (file/...) else "unknown", error in contents */
		const char *ft = st ? asp_type_name(asp_type_from_mode(st->mode)) : "unknown";
		dstr_appendz(&j->out, "{\"type\":\"");
		dstr_appendz(&j->out, ft);
		dstr_appendz(&j->out, "\",\"name\":\"");
		jenc(&j->out, rootpath);
		dstr_appendc(&j->out, '"');
		if (st) { /* meta columns on the failed root; tree zeroes its inode/dev */
			struct asp_statinfo z = *st;
			z.ino = 0;
			z.dev = 0;
			jfillinfo(j, &z);
		}
		dstr_appendz(&j->out, ",\"contents\":[{\"error\": \"error opening dir\"}");
		dstr_appendz(&j->out, jnl(j));
		jindent(j, 0);
		dstr_appendz(&j->out, "]}");
		dstr_appendz(&j->out, last_root ? "" : ",");
		dstr_appendz(&j->out, jnl(j));
		return;
	}

	/* Normally the root is a real directory; --fromfile roots take the type of
	 * the path-list file itself (e.g. "file"). tree leaves the root's inode/dev
	 * at 0 even under --inodes/--device, so zero them on a copy here. */
	const char *rtype = (st && !S_ISDIR(st->mode))
				    ? asp_type_name(asp_type_from_mode(st->mode))
				    : "directory";
	struct asp_statinfo rstz;
	if (st) {
		rstz = *st;
		rstz.ino = 0;
		rstz.dev = 0;
	}
	dstr_appendz(&j->out, "{\"type\":\"");
	dstr_appendz(&j->out, rtype);
	dstr_appendz(&j->out, "\",\"name\":\"");
	jenc(&j->out, rootpath);
	dstr_appendc(&j->out, '"');
	jfillinfo(j, st ? &rstz : NULL);

	/* Root tripped --filelimit: a directory whose only content is the error,
	 * counted as one directory (SR-2.12), mirroring a child over-limit dir. */
	if (limit_err) {
		tot->dirs++;
		dstr_appendz(&j->out, ",\"contents\":[{\"error\": \"");
		jenc(&j->out, limit_err);
		dstr_appendz(&j->out, "\"}");
		dstr_appendz(&j->out, jnl(j)); /* root: error node then newline + indent + ] */
		jindent(j, 0);
		dstr_appendz(&j->out, "]}");
		dstr_appendz(&j->out, last_root ? "" : ",");
		dstr_appendz(&j->out, jnl(j));
		return;
	}

	/* tree counts the root as a directory and emits "contents" only when it has
	 * at least one child; an empty root is just {type,name} and counts 0. */
	if (top && top[0]) {
		tot->dirs++;
		if (j->o->fullpath) { /* seed the -f path stack with the root */
			dstr_clear(&j->fp);
			dstr_appendz(&j->fp, rootpath);
		}
		dstr_appendz(&j->out, ",\"contents\":[");
		dstr_appendz(&j->out, jnl(j));
		jemit_level(j, top, 1, tot);
		jindent(j, 0);
		dstr_appendz(&j->out, "]}");
	} else {
		dstr_appendc(&j->out, '}');
	}
	dstr_appendz(&j->out, last_root ? "" : ",");
	dstr_appendz(&j->out, jnl(j));
	jmaybe(j);
}

static const struct tree_renderer json_vt = {
	json_begin, json_tree, json_report, json_end,
};
const struct renderer asp_json_renderer = { NULL, &json_vt };
