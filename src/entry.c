#include "entry.h"

#include <string.h>

struct entry *entry_new(struct arena *a, const char *name, size_t namelen,
			enum asp_type type)
{
	struct entry *e = arena_alloc(a, sizeof *e + namelen + 1);
	e->namelen = (uint32_t)namelen;
	e->type = (uint16_t)type;
	e->flags = 0;
	e->lnk = NULL;
	e->child = NULL;
	memcpy(e->name, name, namelen);
	e->name[namelen] = '\0';
	return e;
}
