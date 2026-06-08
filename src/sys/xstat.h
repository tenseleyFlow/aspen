#ifndef ASP_SYS_XSTAT_H
#define ASP_SYS_XSTAT_H

/*
 * stat abstraction — the serial provider's metadata path. Only called when a
 * flag needs metadata or d_type was UNKNOWN (.docs/audits/03 §3). statx with a
 * minimal mask (Linux) is wired in a later sprint; this is the fstatat baseline.
 */

#include "sys/dir.h" /* enum asp_type */

#include <sys/types.h>
#include <sys/stat.h>

struct asp_statinfo {
	mode_t mode;
	ino_t ino;
	dev_t dev;
	off_t size;
	uid_t uid;
	gid_t gid;
	time_t mtime, ctime; /* atime/nlink dropped: tree displays neither (SR-4.2) */
};

enum asp_type asp_type_from_mode(mode_t m);

/* fstatat relative to dirfd; follow=0 => AT_SYMLINK_NOFOLLOW. 0 ok, -1 errno. */
int asp_stat_at(int dirfd, const char *name, int follow, struct asp_statinfo *o);

#endif /* ASP_SYS_XSTAT_H */
