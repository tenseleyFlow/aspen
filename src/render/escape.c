#include "render/escape.h"
#include "dstr.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void asp_html_encode(struct dstr *out, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '<': dstr_appendz(out, "&lt;"); break;
		case '>': dstr_appendz(out, "&gt;"); break;
		case '&': dstr_appendz(out, "&amp;"); break;
		case '"': dstr_appendz(out, "&quot;"); break;
		default:  dstr_appendc(out, *s); break;
		}
	}
}

int asp_url_encode_n(struct dstr *out, const char *s, size_t n)
{
	static const char unreserved[] = "/-._~";
	int slash = 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i]; /* signed, like tree */
		if (isalnum((unsigned char)c) || strchr(unreserved, c)) {
			dstr_appendc(out, c);
		} else {
			char b[16];
			int m = snprintf(b, sizeof b, "%%%02X", c); /* sign-extends like tree */
			if (m > 0)
				dstr_append(out, b, (size_t)m);
		}
		slash = (c == '/');
	}
	return slash;
}

int asp_url_encode(struct dstr *out, const char *s)
{
	return asp_url_encode_n(out, s, strlen(s));
}
