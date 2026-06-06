#ifndef ASP_RENDER_ESCAPE_H
#define ASP_RENDER_ESCAPE_H

/* HTML/XML entity escaping (tree's html_encode): < > & " -> entities. */

#include "dstr.h"

#include <stddef.h>

void asp_html_encode(struct dstr *out, const char *s);

/* URL percent-encoding (tree's url_encode): whitelist alnum + "/-._~"; high
 * bytes sign-extend to %FFFFFFXX (tree's signed-char behavior). Returns 1 if the
 * last byte was '/'. Used by OSC-8 hyperlinks and HTML hrefs. */
int asp_url_encode_n(struct dstr *out, const char *s, size_t n);
int asp_url_encode(struct dstr *out, const char *s);

#endif /* ASP_RENDER_ESCAPE_H */
