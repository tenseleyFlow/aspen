#include "render/json.h"
#include "render/fileinfo.h"
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
}

void json_ctx_destroy(struct json_ctx *j)
{
	dstr_free(&j->out);
}

static const char *jnl(struct json_ctx *j)
{
	return j->o->noindent ? "" : "\n";
}

static void jflush(struct json_ctx *j)
{
	size_t off = 0;
	while (off < j->out.len) {
		ssize_t w = write(j->fd, j->out.data + off, j->out.len - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		off += (size_t)w;
	}
	dstr_clear(&j->out);
}

static void jmaybe(struct json_ctx *j)
{
	if (j->out.len >= (64u * 1024u))
		jflush(j);
}

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

static void jindent(struct json_ctx *j, int level)
{
	if (j->o->noindent)
		return;
	for (int i = 0; i <= level; i++)
		dstr_appendz(&j->out, "    ");
}

static const char *ftype_str(enum asp_type t)
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
			asp_psize(nb, o, st->size);
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
	dstr_appendz(&j->out, ftype_str(type));
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
			tot->dirs++;
		else
			tot->files++;

		jindent(j, depth);
		jhead(j, e->type, e->name, e, e->st);

		/* tree emits "contents" only for a NON-empty descended dir (an empty
		 * dir reads as no children -> no contents key) or an unreadable dir. */
		int has_kids = e->child && e->child[0];
		int direrr = dir_like && e->err && !has_kids;
		if (has_kids || direrr) {
			dstr_appendz(&j->out, ",\"contents\":[");
			dstr_appendz(&j->out, jnl(j));
			if (e->child) {
				jemit_level(j, e->child, depth + 1, tot);
			} else { /* unreadable dir: an error object inside contents */
				jindent(j, depth + 1);
				dstr_appendz(&j->out, "{\"error\": \"");
				dstr_appendz(&j->out, e->err);
				dstr_appendz(&j->out, "\"}");
				dstr_appendz(&j->out, jnl(j));
			}
			jindent(j, depth);
			dstr_appendz(&j->out, "]}");
			dstr_appendz(&j->out, last ? "" : ",");
			dstr_appendz(&j->out, jnl(j));
		} else {
			dstr_appendc(&j->out, '}');
			dstr_appendz(&j->out, last ? "" : ",");
			dstr_appendz(&j->out, jnl(j));
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
		      int opened, struct entry **top, struct totals *tot, int last_root)
{
	struct json_ctx *j = ctx;

	jindent(j, 0);
	if (!opened) { /* failed root: unknown type, error inside contents (tree) */
		dstr_appendz(&j->out, "{\"type\":\"unknown\",\"name\":\"");
		jenc(&j->out, rootpath);
		dstr_appendz(&j->out, "\",\"contents\":[{\"error\": \"error opening dir\"}");
		dstr_appendz(&j->out, jnl(j));
		jindent(j, 0);
		dstr_appendz(&j->out, "]}");
		dstr_appendz(&j->out, last_root ? "" : ",");
		dstr_appendz(&j->out, jnl(j));
		return;
	}

	dstr_appendz(&j->out, "{\"type\":\"directory\",\"name\":\"");
	jenc(&j->out, rootpath);
	dstr_appendc(&j->out, '"');
	jfillinfo(j, st);
	dstr_appendz(&j->out, ",\"contents\":[");
	dstr_appendz(&j->out, jnl(j));
	tot->dirs++; /* root counts as a directory */

	jemit_level(j, top, 1, tot);

	jindent(j, 0);
	dstr_appendz(&j->out, "]}");
	dstr_appendz(&j->out, last_root ? "" : ",");
	dstr_appendz(&j->out, jnl(j));
	jmaybe(j);
}

const struct renderer asp_json_renderer = {
	json_begin, NULL, NULL, NULL, NULL, NULL, json_report, json_end, json_tree,
};
