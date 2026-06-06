#include "test.h"
#include "arena.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
	struct arena a;
	arena_init(&a, 256); /* small slabs to force growth paths */

	char *p1 = arena_alloc(&a, 10);
	memset(p1, 'x', 10);
	CHECK("max-aligned", ((uintptr_t)p1 % _Alignof(max_align_t)) == 0);

	char *s = arena_strdup(&a, "hello");
	CHECK_STR("strdup", s, "hello");
	CHECK("strdup is a copy", s != NULL);

	/* span many slabs, verify integrity */
	int ok = 1;
	for (int i = 0; i < 1000; i++) {
		int *q = arena_alloc(&a, sizeof(int) * 8);
		q[0] = i;
		q[7] = i;
		if (q[0] != i || q[7] != i)
			ok = 0;
	}
	CHECK("slab spanning integrity", ok);

	/* mark/rewind: parent persists while child region is reclaimed */
	char *parent = arena_strdup(&a, "PARENT");
	struct arena_marker child = arena_mark(&a);
	char *c1 = arena_strdup(&a, "child-data-aaaa");
	(void)c1;
	arena_rewind(&a, child);
	char *c2 = arena_strdup(&a, "other");
	(void)c2;
	CHECK_STR("parent intact after child rewind", parent, "PARENT");

	/* allocation larger than a slab */
	char *big = arena_alloc(&a, 4096);
	memset(big, 'Z', 4096);
	CHECK("oversized alloc", big[0] == 'Z' && big[4095] == 'Z');

	/* reset reuses the head slab */
	arena_reset(&a);
	char *r = arena_alloc(&a, 10);
	CHECK("reset reuses head", r == p1);

	arena_destroy(&a);
	return test_summary("arena");
}
