#include "hashtab.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

struct inoslot {
	ino_t ino;
	dev_t dev;
	int used;
};

void inoset_init(struct inoset *s)
{
	s->cap = 64;
	s->len = 0;
	s->slots = asp_xmalloc(s->cap * sizeof *s->slots);
	memset(s->slots, 0, s->cap * sizeof *s->slots);
}

void inoset_destroy(struct inoset *s)
{
	free(s->slots);
	s->slots = NULL;
	s->cap = s->len = 0;
}

static size_t mix(ino_t ino, dev_t dev, size_t mask)
{
	unsigned long long h = (unsigned long long)ino * 1099511628211ULL;
	h ^= (unsigned long long)dev + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	return (size_t)h & mask;
}

static int find_slot(struct inoslot *slots, size_t cap, ino_t ino, dev_t dev)
{
	size_t mask = cap - 1;
	size_t i = mix(ino, dev, mask);
	for (;;) {
		if (!slots[i].used)
			return (int)i; /* empty: not present */
		if (slots[i].ino == ino && slots[i].dev == dev)
			return (int)i; /* present */
		i = (i + 1) & mask;
	}
}

static void grow(struct inoset *s)
{
	size_t ncap = s->cap * 2;
	struct inoslot *ns = asp_xmalloc(ncap * sizeof *ns);
	memset(ns, 0, ncap * sizeof *ns);
	for (size_t i = 0; i < s->cap; i++) {
		if (s->slots[i].used) {
			int j = find_slot(ns, ncap, s->slots[i].ino, s->slots[i].dev);
			ns[j] = s->slots[i];
		}
	}
	free(s->slots);
	s->slots = ns;
	s->cap = ncap;
}

int inoset_has(const struct inoset *s, ino_t ino, dev_t dev)
{
	int i = find_slot(s->slots, s->cap, ino, dev);
	return s->slots[i].used;
}

int inoset_add(struct inoset *s, ino_t ino, dev_t dev)
{
	if ((s->len + 1) * 4 >= s->cap * 3) /* keep load < 0.75 */
		grow(s);
	int i = find_slot(s->slots, s->cap, ino, dev);
	if (s->slots[i].used)
		return 0;
	s->slots[i].used = 1;
	s->slots[i].ino = ino;
	s->slots[i].dev = dev;
	s->len++;
	return 1;
}
