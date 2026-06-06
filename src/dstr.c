#include "dstr.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

void dstr_init(struct dstr *s)
{
	s->data = NULL;
	s->len = 0;
	s->cap = 0;
}

void dstr_free(struct dstr *s)
{
	free(s->data);
	s->data = NULL;
	s->len = s->cap = 0;
}

void dstr_clear(struct dstr *s)
{
	s->len = 0;
	if (s->data)
		s->data[0] = '\0';
}

void dstr_reserve(struct dstr *s, size_t extra)
{
	size_t need = asp_size_add(s->len, asp_size_add(extra, 1)); /* + NUL */
	if (need <= s->cap)
		return;
	size_t cap = asp_bit_ceil(need);
	if (cap < 16)
		cap = 16;
	s->data = asp_xrealloc(s->data, cap);
	s->cap = cap;
}

void dstr_append(struct dstr *s, const char *p, size_t n)
{
	dstr_reserve(s, n);
	memcpy(s->data + s->len, p, n);
	s->len += n;
	s->data[s->len] = '\0';
}

void dstr_appendz(struct dstr *s, const char *z)
{
	dstr_append(s, z, strlen(z));
}

void dstr_appendc(struct dstr *s, char c)
{
	dstr_reserve(s, 1);
	s->data[s->len++] = c;
	s->data[s->len] = '\0';
}
