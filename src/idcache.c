#include "idcache.h"
#include "util.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct identry {
	unsigned long id;
	char *name;
};

/* Small caches; trees rarely span many distinct ids, so linear scan is fine. */
static struct identry *ucache;
static size_t un, ucap;
static struct identry *gcache;
static size_t gn, gcap;

static char *dup_str(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = asp_xmalloc(n);
	memcpy(d, s, n);
	return d;
}

static const char *lookup(struct identry **arr, size_t *n, size_t *cap,
			  unsigned long id, int is_uid)
{
	for (size_t i = 0; i < *n; i++)
		if ((*arr)[i].id == id)
			return (*arr)[i].name;

	char *name;
	if (is_uid) {
		struct passwd *p = getpwuid((uid_t)id);
		name = p ? dup_str(p->pw_name) : NULL;
	} else {
		struct group *g = getgrgid((gid_t)id);
		name = g ? dup_str(g->gr_name) : NULL;
	}
	if (!name) {
		char buf[32];
		snprintf(buf, sizeof buf, "%d", (int)id); /* matches tree's %d */
		name = dup_str(buf);
	}

	if (*n == *cap) {
		*cap = *cap ? *cap * 2 : 16;
		*arr = asp_xrealloc(*arr, *cap * sizeof **arr);
	}
	(*arr)[*n].id = id;
	(*arr)[*n].name = name;
	(*n)++;
	return name;
}

const char *uidtoname(uid_t uid)
{
	return lookup(&ucache, &un, &ucap, (unsigned long)uid, 1);
}

const char *gidtoname(gid_t gid)
{
	return lookup(&gcache, &gn, &gcap, (unsigned long)gid, 0);
}
