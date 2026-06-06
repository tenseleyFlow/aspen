#include "render/fileinfo.h"
#include "idcache.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* type chars in the same order as the mode test below */
static const mode_t ifmt[] = { S_IFREG, S_IFDIR, S_IFLNK, S_IFCHR,
			       S_IFBLK, S_IFSOCK, S_IFIFO, 0 };
static const char fmt[] = "-dlcbsp?";

static const char *prot(mode_t m)
{
	static char buf[11];
	static const char perms[] = "rwxrwxrwx";
	int i;
	for (i = 0; ifmt[i] && (m & S_IFMT) != ifmt[i]; i++)
		;
	buf[0] = fmt[i];
	mode_t b;
	for (b = S_IRUSR, i = 0; i < 9; b >>= 1, i++)
		buf[i + 1] = (m & b) ? perms[i] : '-';
	if (m & S_ISUID) buf[3] = (buf[3] == '-') ? 'S' : 's';
	if (m & S_ISGID) buf[6] = (buf[6] == '-') ? 'S' : 's';
	if (m & S_ISVTX) buf[9] = (buf[9] == '-') ? 'T' : 't';
	buf[10] = '\0';
	return buf;
}

int asp_psize(char *buf, const struct options *o, off_t size)
{
	static const char iec[] = "BKMGTPEZY", si[] = "dkMGTPEZY";
	const char *unit = o->siflag ? si : iec;
	int usize = o->siflag ? 1000 : 1024;

	if (o->humanflag || o->siflag) {
		int idx;
		for (idx = size < usize ? 0 : 1; size >= (off_t)(usize * usize); idx++, size /= usize)
			;
		if (!idx)
			return sprintf(buf, " %4d", (int)size);
		return sprintf(buf, (((size + 52) / usize) >= 10) ? " %3.0f%c" : " %3.1f%c",
			       (double)size / (double)usize, unit[idx]);
	}
	return sprintf(buf, sizeof(off_t) == sizeof(long long) ? " %11lld" : " %9lld",
		       (long long)size);
}

#define SIXMONTHS (6 * 31 * 24 * 60 * 60)

static const char *do_date(const struct options *o, time_t t)
{
	static char buf[256];
	struct tm *tm = localtime(&t);
	if (o->timefmt) {
		strftime(buf, 255, o->timefmt, tm);
		buf[255] = '\0';
	} else {
		time_t now = time(NULL);
		if (t > now || (t + SIXMONTHS) < now)
			strftime(buf, 255, "%b %e  %Y", tm);
		else
			strftime(buf, 255, "%b %e %R", tm);
	}
	return buf;
}

size_t asp_fillinfo(char *buf, size_t bufsz, const struct options *o,
		    const struct asp_statinfo *st)
{
	int n = 0;
	buf[0] = '\0';
	if (!st)
		return 0;

	if (o->inodeflag)
		n += snprintf(buf + n, bufsz - (size_t)n, " %7lld", (long long)st->ino);
	if (o->devflag)
		n += snprintf(buf + n, bufsz - (size_t)n, " %3d", (int)st->dev);
	if (o->permflag)
		n += snprintf(buf + n, bufsz - (size_t)n, " %s", prot(st->mode));
	if (o->userflag)
		n += snprintf(buf + n, bufsz - (size_t)n, " %-8.32s", uidtoname(st->uid));
	if (o->groupflag)
		n += snprintf(buf + n, bufsz - (size_t)n, " %-8.32s", gidtoname(st->gid));
	if (o->sizeflag)
		n += asp_psize(buf + n, o, st->size);
	if (o->dateflag)
		n += snprintf(buf + n, bufsz - (size_t)n, " %s",
			      do_date(o, o->ctimeflag ? st->ctime : st->mtime));

	if (buf[0] == ' ') {
		buf[0] = '[';
		n += snprintf(buf + n, bufsz - (size_t)n, "]");
	}
	return (size_t)n;
}
