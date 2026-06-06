#include "entry.h"

#include <string.h>

struct entry *entry_new(struct arena *a, const char *name, size_t namelen,
			enum asp_type type)
{
	struct entry *e = arena_alloc(a, sizeof *e + namelen + 1);
	e->namelen = (uint32_t)namelen;
	e->type = (uint16_t)type;
	e->ltype = ASP_UNKNOWN;
	e->flags = 0;
	e->_pad = 0;
	e->ino = 0;
	e->dev = 0;
	e->lmode = 0;
	e->st = NULL;
	e->lnk = NULL;
	e->child = NULL;
	e->err = NULL;
	memcpy(e->name, name, namelen);
	e->name[namelen] = '\0';
	return e;
}
