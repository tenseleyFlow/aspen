#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tree's exact OOM diagnostic + exit code (program name aside). */
static void oom(void)
{
	fputs("aspen: virtual memory exhausted.\n", stderr);
	exit(1);
}

void *asp_xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p)
		oom();
	return p;
}

void *asp_xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q)
		oom();
	return q;
}

char *asp_strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = asp_xmalloc(n);
	memcpy(d, s, n);
	return d;
}

size_t asp_size_add(size_t a, size_t b)
{
	size_t r = a + b;
	return r < a ? (size_t)-1 : r;
}

size_t asp_size_mul(size_t a, size_t b)
{
	if (a == 0 || b == 0)
		return 0;
	size_t r = a * b;
	return r / a != b ? (size_t)-1 : r;
}

unsigned asp_bit_width(size_t x)
{
	unsigned w = 0;
	while (x) {
		w++;
		x >>= 1;
	}
	return w;
}

size_t asp_bit_ceil(size_t x)
{
	if (x <= 1)
		return 1;
	return (size_t)1 << asp_bit_width(x - 1);
}
