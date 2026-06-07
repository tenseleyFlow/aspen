#ifndef ASP_UTIL_H
#define ASP_UTIL_H

#include <stddef.h>

/* Allocation that aborts on failure (CLI policy: OOM is fatal — tree's exact
 * "virtual memory exhausted." diagnostic and exit(1), program name aside). */
void *asp_xmalloc(size_t n);
void *asp_xrealloc(void *p, size_t n);

/* strdup via asp_xmalloc (so OOM is handled uniformly). */
char *asp_strdup(const char *s);

/* Saturating size arithmetic — clamp to SIZE_MAX on overflow instead of wrapping. */
size_t asp_size_add(size_t a, size_t b);
size_t asp_size_mul(size_t a, size_t b);

/* bit_width(0)=0, else floor(log2(x))+1.  bit_ceil(x)=smallest power of two >= x (>=1). */
unsigned asp_bit_width(size_t x);
size_t   asp_bit_ceil(size_t x);

#endif /* ASP_UTIL_H */
