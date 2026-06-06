#include "render/name.h"
#include "dstr.h"
#include "util.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

static void esc_octal(struct dstr *o, unsigned long v)
{
	char b[16];
	int n = snprintf(b, sizeof b, "\\%03lo", v & 0xffffffffUL);
	if (n > 0)
		dstr_append(o, b, (size_t)n);
}

static int byte_branch(struct dstr *o, const char *s, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		if ((c >= 7 && c <= 13) || c == '\\' || c == ' ') {
			dstr_appendc(o, '\\');
			if (c > 13)
				dstr_appendc(o, (char)c); /* '\\' or ' ' */
			else
				dstr_appendc(o, "abtnvfr"[c - 7]);
		} else if (isprint(c)) {
			dstr_appendc(o, (char)c);
		} else {
			esc_octal(o, c);
		}
	}
	return 0;
}

void name_print(struct dstr *out, const char *s, size_t len, int mb_cur_max)
{
	if (mb_cur_max > 1) {
		wchar_t *ws = asp_xmalloc((len + 1) * sizeof *ws);
		size_t k = mbstowcs(ws, s, len + 1);
		if (k != (size_t)-1) {
			char mb[MB_LEN_MAX];
			wctomb(NULL, 0); /* reset shift state */
			for (size_t i = 0; i < k; i++) {
				if (iswprint((wint_t)ws[i])) {
					int n = wctomb(mb, ws[i]);
					if (n > 0)
						dstr_append(out, mb, (size_t)n);
				} else {
					esc_octal(out, (unsigned long)ws[i]);
				}
			}
			free(ws);
			return;
		}
		free(ws);
		/* mbstowcs failed (invalid sequence): fall back to byte handling */
	}
	byte_branch(out, s, len);
}
