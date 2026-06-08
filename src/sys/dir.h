#ifndef ASP_SYS_DIR_H
#define ASP_SYS_DIR_H

/*
 * Directory reading abstraction — the serial I/O provider's read path.
 * Backends (selected at configure time): raw getdents64 (Linux), getdirentries
 * (FreeBSD/macOS), readdir(3) fallback. All return (name, type) with type taken
 * from the dirent's d_type so the common path needs no stat (.docs/audits/03 §2-3).
 *
 * "." and ".." are skipped by the reader. Dotfile (-a) policy lives in traverse.
 */

#include <stddef.h>

enum asp_type {
	ASP_UNKNOWN = 0,
	ASP_DIR,
	ASP_REG,
	ASP_LNK,
	ASP_FIFO,
	ASP_SOCK,
	ASP_CHR,
	ASP_BLK,
	ASP_WHT, /* whiteout (union mounts) */
};

struct asp_dir;

struct asp_dirent {
	const char *name; /* valid until the next asp_dirread on this dir */
	size_t namelen;   /* strlen(name); from d_namlen where the platform has it */
	enum asp_type type;
};

/* Open a directory. *out is set on success. Returns 0, or -1 with errno set. */
int asp_diropen(const char *path, struct asp_dir **out);
int asp_diropen_at(int parent_fd, const char *name, struct asp_dir **out);

/* Read next entry: 1 = got one, 0 = end, -1 = error (errno set). */
int asp_dirread(struct asp_dir *d, struct asp_dirent *e);

int asp_dirfd(const struct asp_dir *d);
void asp_dirclose(struct asp_dir *d);

#endif /* ASP_SYS_DIR_H */
