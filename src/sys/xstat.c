#include "sys/xstat.h"

#include <fcntl.h>

enum asp_type asp_type_from_mode(mode_t m)
{
	switch (m & S_IFMT) {
	case S_IFDIR:  return ASP_DIR;
	case S_IFREG:  return ASP_REG;
	case S_IFLNK:  return ASP_LNK;
	case S_IFIFO:  return ASP_FIFO;
	case S_IFSOCK: return ASP_SOCK;
	case S_IFCHR:  return ASP_CHR;
	case S_IFBLK:  return ASP_BLK;
	default:       return ASP_UNKNOWN;
	}
}

int asp_stat_at(int dirfd, const char *name, int follow, struct asp_statinfo *o)
{
	struct stat st;
	int flags = follow ? 0 : AT_SYMLINK_NOFOLLOW;
	if (fstatat(dirfd, name, &st, flags) < 0)
		return -1;
	o->mode = st.st_mode;
	o->ino = st.st_ino;
	o->dev = st.st_dev;
	o->size = st.st_size;
	o->uid = st.st_uid;
	o->gid = st.st_gid;
	o->nlink = st.st_nlink;
	o->atime = st.st_atime;
	o->mtime = st.st_mtime;
	o->ctime = st.st_ctime;
	return 0;
}
