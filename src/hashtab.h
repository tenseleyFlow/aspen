#ifndef ASP_HASHTAB_H
#define ASP_HASHTAB_H

/*
 * Open-addressed set of (inode, device) pairs, for symlink cycle detection
 * under -l (tree's saveino/findino, hash.c). Mirrors tree's global-set behavior.
 */

#include <sys/types.h>
#include <stddef.h>

struct inoset {
	struct inoslot *slots;
	size_t cap;  /* power of two */
	size_t len;
};

void inoset_init(struct inoset *s);
void inoset_destroy(struct inoset *s);
int inoset_has(const struct inoset *s, ino_t ino, dev_t dev);
/* add; returns 1 if newly inserted, 0 if already present */
int inoset_add(struct inoset *s, ino_t ino, dev_t dev);

#endif /* ASP_HASHTAB_H */
