#include "sys/dir.h"
#include "util.h"
#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(ASP_DIR_BACKEND_getdents64)
#include <sys/syscall.h>
#endif

#define DIRBUF (64u * 1024u)

struct asp_dir {
	int fd;
#if defined(ASP_DIR_BACKEND_readdir)
	DIR *dp;
#else
	size_t pos;  /* cursor within buf */
	size_t size; /* valid bytes in buf */
	int eof;
	/* dirent records hold 8-byte ino fields; the buffer (and thus each
	 * kernel-aligned record) must be suitably aligned. */
	_Alignas(max_align_t) char buf[DIRBUF];
#endif
};

#if defined(ASP_DIR_BACKEND_getdents64)
/* Linux: raw getdents64 + linux_dirent64 (avoids the readdir(3) per-entry copy). */
struct linux_dirent64 {
	unsigned long long d_ino;
	long long d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};
static ssize_t sys_getdents(int fd, void *buf, size_t n)
{
	return syscall(SYS_getdents64, fd, buf, n);
}
#endif

/* Unused only on the readdir backend without d_type (rare); keep it warning-free
 * there without a backend-specific guard. */
__attribute__((unused)) static enum asp_type type_from_dt(unsigned dt)
{
	switch (dt) {
	case DT_DIR:  return ASP_DIR;
	case DT_REG:  return ASP_REG;
	case DT_LNK:  return ASP_LNK;
	case DT_FIFO: return ASP_FIFO;
	case DT_SOCK: return ASP_SOCK;
	case DT_CHR:  return ASP_CHR;
	case DT_BLK:  return ASP_BLK;
#ifdef DT_WHT
	case DT_WHT:  return ASP_WHT;
#endif
	default:      return ASP_UNKNOWN;
	}
}

static int is_dotdir(const char *n)
{
	return n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0'));
}

static struct asp_dir *dir_from_fd(int fd)
{
	struct asp_dir *d = asp_xmalloc(sizeof *d);
	d->fd = fd;
#if defined(ASP_DIR_BACKEND_readdir)
	d->dp = fdopendir(fd);
	if (!d->dp) {
		int e = errno;
		close(fd);
		free(d);
		errno = e;
		return NULL;
	}
#else
	d->pos = d->size = 0;
	d->eof = 0;
#endif
	return d;
}

int asp_diropen(const char *path, struct asp_dir **out)
{
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	struct asp_dir *d = dir_from_fd(fd);
	if (!d)
		return -1;
	*out = d;
	return 0;
}

int asp_diropen_at(int parent_fd, const char *name, struct asp_dir **out)
{
	int fd = openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	struct asp_dir *d = dir_from_fd(fd);
	if (!d)
		return -1;
	*out = d;
	return 0;
}

int asp_dirfd(const struct asp_dir *d)
{
	return d->fd;
}

void asp_dirclose(struct asp_dir *d)
{
	if (!d)
		return;
#if defined(ASP_DIR_BACKEND_readdir)
	closedir(d->dp); /* also closes fd */
#else
	close(d->fd);
#endif
	free(d);
}

#if defined(ASP_DIR_BACKEND_readdir)

int asp_dirread(struct asp_dir *d, struct asp_dirent *e)
{
	for (;;) {
		errno = 0;
		struct dirent *de = readdir(d->dp);
		if (!de)
			return errno ? -1 : 0;
		if (is_dotdir(de->d_name))
			continue;
#if ASP_HAS_D_TYPE
		e->type = type_from_dt(de->d_type);
#else
		e->type = ASP_UNKNOWN;
#endif
		e->name = de->d_name;
		return 1;
	}
}

#else /* getdents64 / getdirentries: read into a big buffer, walk records */

static int refill(struct asp_dir *d)
{
	if (d->eof)
		return 0;
	ssize_t r;
#if defined(ASP_DIR_BACKEND_getdents64)
	r = sys_getdents(d->fd, d->buf, sizeof d->buf);
#else
	long base;
	r = getdirentries(d->fd, d->buf, sizeof d->buf, &base);
#endif
	if (r < 0)
		return -1;
	if (r == 0) {
		d->eof = 1;
		return 0;
	}
	d->pos = 0;
	d->size = (size_t)r;
	return 1;
}

int asp_dirread(struct asp_dir *d, struct asp_dirent *e)
{
	for (;;) {
		if (d->pos >= d->size) {
			int rc = refill(d);
			if (rc <= 0)
				return rc; /* 0 eof, -1 err */
			continue;
		}
#if defined(ASP_DIR_BACKEND_getdents64)
		struct linux_dirent64 *de = (void *)(d->buf + d->pos);
		d->pos += de->d_reclen;
		if (de->d_ino == 0 || is_dotdir(de->d_name))
			continue;
		e->type = type_from_dt(de->d_type);
		e->name = de->d_name;
#else
		struct dirent *de = (void *)(d->buf + d->pos);
		d->pos += de->d_reclen;
		if (de->d_reclen == 0) { /* defensive: avoid infinite loop */
			d->pos = d->size;
			continue;
		}
		if (de->d_fileno == 0 || is_dotdir(de->d_name))
			continue;
		e->type = type_from_dt(de->d_type);
		e->name = de->d_name;
#endif
		return 1;
	}
}

#endif
