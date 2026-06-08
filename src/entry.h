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
	ENT_MATCHED = 1u << 4, /* --matchdirs: dir name matched -P (prune-protected) */
	ENT_STAT_FAILED = 1u << 5, /* deferred stat pass: lstat failed; drop in order */
	ENT_RECURSIVE = 1u << 6,   /* -l: symlink target already seen ("recursive, not
				    * followed"). tree's descend==-1 -> a depth-scaled close
				    * indent in -J/-X, vs an unreadable dir's fixed close. */
};

struct entry {
	uint32_t namelen;
	uint16_t type;  /* enum asp_type (from d_type/lstat) */
	uint16_t ltype; /* symlink target type when stat-followed, else ASP_UNKNOWN */
	uint16_t flags;
	uint16_t _pad;
	uint32_t condensed;   /* --condense: count of singleton dirs absorbed into this
			       * one (added back to the dir total at emit); fills the
			       * alignment gap before ino, so it costs no extra bytes */
	ino_t ino;            /* cycle/xdev identity: target for links, own otherwise */
	dev_t dev;
	mode_t lmode;         /* symlink target mode (for color/-F), when followed */
	const struct asp_statinfo *st; /* lstat info for -s/-p/-u/-g/-D columns; NULL otherwise */
	char *lnk;            /* symlink target string, or NULL */
	struct entry **child; /* full-tree mode: NULL-terminated child array, or NULL */
	const char *err;      /* full-tree mode: per-entry error to render, or NULL */
	char **info;          /* --info: NULL-terminated annotation lines, or NULL */
	const char *condensed_name; /* --condense: collapsed "a/b/c" path shown in place
				     * of name (and pushed onto the path), else NULL */
	char name[];          /* inline, NUL-terminated */
};

struct entry *entry_new(struct arena *a, const char *name, size_t namelen,
			enum asp_type type);

/* tree's long type name ("directory"/"file"/"link"/...), used by the JSON and
 * XML renderers (shared so json's ftype_str and xml's tag_str can't drift). */
const char *asp_type_name(enum asp_type t);

#endif /* ASP_ENTRY_H */
