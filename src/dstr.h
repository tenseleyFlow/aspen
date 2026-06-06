#ifndef ASP_DSTR_H
#define ASP_DSTR_H

#include <stddef.h>

/*
 * Growable byte string with power-of-two growth — the output buffer and the
 * running path/indent buffers (see .docs/audits/03 §5). Always NUL-terminated
 * for convenience; len excludes the NUL. Appends take an explicit length to
 * avoid strlen churn in hot loops.
 */
struct dstr {
	char *data;
	size_t len;
	size_t cap;
};

void dstr_init(struct dstr *s);
void dstr_free(struct dstr *s);
void dstr_clear(struct dstr *s); /* len = 0, keep capacity */

void dstr_reserve(struct dstr *s, size_t extra); /* room for `extra` more bytes + NUL */
void dstr_append(struct dstr *s, const char *p, size_t n);
void dstr_appendz(struct dstr *s, const char *z);
void dstr_appendc(struct dstr *s, char c);

#endif /* ASP_DSTR_H */
