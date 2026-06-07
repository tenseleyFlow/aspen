#include "entry.h"

#include <string.h>

const char *asp_type_name(enum asp_type t)
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

struct entry *entry_new(struct arena *a, const char *name, size_t namelen,
			enum asp_type type)
{
	struct entry *e = arena_alloc(a, sizeof *e + namelen + 1);
	e->namelen = (uint32_t)namelen;
	e->type = (uint16_t)type;
	e->ltype = ASP_UNKNOWN;
	e->flags = 0;
	e->_pad = 0;
	e->condensed = 0;
	e->ino = 0;
	e->dev = 0;
	e->lmode = 0;
	e->st = NULL;
	e->lnk = NULL;
	e->child = NULL;
	e->err = NULL;
	e->info = NULL;
	e->condensed_name = NULL;
	memcpy(e->name, name, namelen);
	e->name[namelen] = '\0';
	return e;
}
