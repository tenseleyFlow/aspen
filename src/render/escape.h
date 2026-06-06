#ifndef ASP_RENDER_ESCAPE_H
#define ASP_RENDER_ESCAPE_H

/* HTML/XML entity escaping (tree's html_encode): < > & " -> entities. */

#include "dstr.h"

void asp_html_encode(struct dstr *out, const char *s);

#endif /* ASP_RENDER_ESCAPE_H */
