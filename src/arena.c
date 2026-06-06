#include "arena.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

struct ablock {
	struct ablock *next;
	size_t size; /* payload bytes in data[] */
	size_t off;  /* bump cursor within data[] */
	_Alignas(max_align_t) char data[];
};

#define ARENA_DEFAULT (64u * 1024u)
#define MAXALIGN _Alignof(max_align_t)

void arena_init(struct arena *a, size_t default_payload)
{
	a->head = a->cur = NULL;
	a->default_payload = default_payload ? default_payload : ARENA_DEFAULT;
}

static struct ablock *block_new(size_t payload)
{
	struct ablock *b = asp_xmalloc(sizeof *b + payload);
	b->next = NULL;
	b->size = payload;
	b->off = 0;
	return b;
}

void *arena_alloc_aligned(struct arena *a, size_t size, size_t align)
{
	if (align < 1)
		align = 1;

	for (;;) {
		struct ablock *c = a->cur;
		if (c) {
			size_t aligned = (c->off + (align - 1)) & ~(align - 1);
			if (aligned >= c->off && aligned + size <= c->size) {
				c->off = aligned + size;
				return c->data + aligned;
			}
			/* current slab is full: advance, reuse, or insert */
			if (c->next && size <= c->next->size) {
				c->next->off = 0;
				a->cur = c->next;
				continue;
			}
			size_t payload = a->default_payload;
			if (asp_size_add(size, align) > payload)
				payload = asp_size_add(size, align);
			struct ablock *nb = block_new(payload);
			nb->next = c->next; /* splice in (keeps any small leftover for reuse) */
			c->next = nb;
			a->cur = nb;
			continue;
		}
		/* empty arena */
		size_t payload = a->default_payload;
		if (asp_size_add(size, align) > payload)
			payload = asp_size_add(size, align);
		a->head = a->cur = block_new(payload);
	}
}

void *arena_alloc(struct arena *a, size_t size)
{
	return arena_alloc_aligned(a, size, MAXALIGN);
}

void *arena_memdup(struct arena *a, const void *p, size_t n)
{
	void *d = arena_alloc(a, n);
	memcpy(d, p, n);
	return d;
}

char *arena_strdup(struct arena *a, const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = arena_alloc_aligned(a, n, 1);
	memcpy(d, s, n);
	return d;
}

struct arena_marker arena_mark(struct arena *a)
{
	struct arena_marker m;
	m.blk = a->cur;
	m.off = a->cur ? a->cur->off : 0;
	return m;
}

void arena_rewind(struct arena *a, struct arena_marker m)
{
	if (m.blk) {
		a->cur = m.blk;
		m.blk->off = m.off;
	} else {
		a->cur = a->head;
		if (a->head)
			a->head->off = 0;
	}
}

void arena_reset(struct arena *a)
{
	a->cur = a->head;
	if (a->head)
		a->head->off = 0;
}

void arena_destroy(struct arena *a)
{
	struct ablock *b = a->head;
	while (b) {
		struct ablock *n = b->next;
		free(b);
		b = n;
	}
	a->head = a->cur = NULL;
}
