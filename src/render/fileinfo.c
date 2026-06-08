#include "render/fileinfo.h"
#include "idcache.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* type chars in the same order as the mode test below */
static const mode_t ifmt[] = { S_IFREG, S_IFDIR, S_IFLNK, S_IFCHR,
			       S_IFBLK, S_IFSOCK, S_IFIFO, 0 };
static const char fmt[] = "-dlcbsp?";

const char *asp_prot(mode_t m)
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

int asp_psize(char *buf, size_t bufsz, const struct options *o, off_t size)
{
	static const char iec[] = "BKMGTPEZY", si[] = "dkMGTPEZY";
	const char *unit = o->siflag ? si : iec;
	int usize = o->siflag ? 1000 : 1024;

	if (o->humanflag || o->siflag) {
		int idx;
		for (idx = size < usize ? 0 : 1; size >= (off_t)(usize * usize); idx++, size /= usize)
			;
		if (!idx)
			return snprintf(buf, bufsz, " %4d", (int)size);
		return snprintf(buf, bufsz, (((size + 52) / usize) >= 10) ? " %3.0f%c" : " %3.1f%c",
				(double)size / (double)usize, unit[idx]);
	}
	return snprintf(buf, bufsz, sizeof(off_t) == sizeof(long long) ? " %11lld" : " %9lld",
			(long long)size);
}

#define SIXMONTHS (6 * 31 * 24 * 60 * 60)

const char *asp_do_date(const struct options *o, time_t t)
{
	static char buf[256];
	struct tm tmbuf;
	struct tm *tm = localtime_r(&t, &tmbuf); /* stack tm, no shared static */
	if (!tm)
		return "";
	if (o->timefmt) {
		strftime(buf, 255, o->timefmt, tm);
		buf[255] = '\0';
	} else {
		/* tree computes the reference "now" once for the whole run; cache it so
		 * the recent/old branch costs no time() syscall per entry. */
		static time_t now;
		static int have_now;
		if (!have_now) {
			now = time(NULL);
			have_now = 1;
		}
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
	/* Each column is appended with bounded writes; n is advanced by the bytes
	 * ACTUALLY written (snprintf/asp_psize report the would-have-written count,
	 * which can exceed the remaining space for a long --timefmt). Clamping here
	 * is what prevents an out-of-bounds write and an over-long return that the
	 * caller would then over-read. The buffer matches tree's info[512] so no
	 * truncation occurs for real inputs (do_date caps the date at 255). */
	size_t n = 0;
	if (bufsz == 0)
		return 0;
	buf[0] = '\0';
	if (!st)
		return 0;

#define ADV(call)                                                       \
	do {                                                            \
		if (n + 1 < bufsz) {                                    \
			int _r = (call);                               \
			if (_r > 0) {                                  \
				size_t _w = (size_t)_r;                \
				if (_w > bufsz - 1 - n)                \
					_w = bufsz - 1 - n;            \
				n += _w;                               \
			}                                              \
		}                                                      \
	} while (0)

	if (o->inodeflag)
		ADV(snprintf(buf + n, bufsz - n, " %7lld", (long long)st->ino));
	if (o->devflag)
		ADV(snprintf(buf + n, bufsz - n, " %3d", (int)st->dev));
	if (o->permflag)
		ADV(snprintf(buf + n, bufsz - n, " %s", asp_prot(st->mode)));
	if (o->userflag)
		ADV(snprintf(buf + n, bufsz - n, " %-8.32s", uidtoname(st->uid)));
	if (o->groupflag)
		ADV(snprintf(buf + n, bufsz - n, " %-8.32s", gidtoname(st->gid)));
	if (o->sizeflag)
		ADV(asp_psize(buf + n, bufsz - n, o, st->size));
	if (o->dateflag)
		ADV(snprintf(buf + n, bufsz - n, " %s",
			     asp_do_date(o, o->ctimeflag ? st->ctime : st->mtime)));

	if (buf[0] == ' ') {
		buf[0] = '[';
		ADV(snprintf(buf + n, bufsz - n, "]"));
	}
#undef ADV
	return n;
}
