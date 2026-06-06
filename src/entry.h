#ifndef ASP_ENTRY_H
#define ASP_ENTRY_H

/*
 * One directory entry. Arena-allocated with the name inline (one allocation per
 * entry, cache-friendly — .docs/audits/03 §4). Metadata is filled lazily only
 * when a flag forces a stat; the common path leaves it untouched.
 */

#include "arena.h"
#include "sys/dir.h"   /* enum asp_type */
#include "sys/xstat.h" /* struct asp_statinfo */

#include <stdint.h>
#include <sys/types.h>

enum {
	ENT_STATTED = 1u << 0,
	ENT_ORPHAN  = 1u << 1, /* dangling symlink */
	ENT_EXEC    = 1u << 2, /* regular file is executable (-F) */
	ENT_LEXEC   = 1u << 3, /* symlink target is executable (-F) */
};

struct entry {
	uint32_t namelen;
	uint16_t type;  /* enum asp_type (from d_type/lstat) */
	uint16_t ltype; /* symlink target type when stat-followed, else ASP_UNKNOWN */
	uint16_t flags;
	uint16_t _pad;
	ino_t ino;            /* cycle/xdev identity: target for links, own otherwise */
	dev_t dev;
	const struct asp_statinfo *st; /* lstat info for -s/-p/-u/-g/-D columns; NULL otherwise */
	char *lnk;            /* symlink target string, or NULL */
	struct entry **child; /* full-tree mode only; NULL while streaming */
	char name[];          /* inline, NUL-terminated */
};

struct entry *entry_new(struct arena *a, const char *name, size_t namelen,
			enum asp_type type);

#endif /* ASP_ENTRY_H */
