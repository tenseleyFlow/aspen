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

static void byte_branch(struct dstr *o, const char *s, size_t len, int mb_cur_max, int flags)
{
	int quote = flags & NP_QUOTE, qmark = flags & NP_QMARK;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		if ((c >= 7 && c <= 13) || c == '\\' || (c == '"' && quote) ||
		    (c == ' ' && !quote)) {
			dstr_appendc(o, '\\');
			if (c > 13)
				dstr_appendc(o, (char)c); /* '\\', '"', or ' ' */
			else
				dstr_appendc(o, "abtnvfr"[c - 7]);
		} else if (isprint(c)) {
			dstr_appendc(o, (char)c);
		} else if (qmark) {
			/* tree keeps high bytes under -q in a multibyte locale */
			if (mb_cur_max > 1 && c > 127)
				dstr_appendc(o, (char)c);
			else
				dstr_appendc(o, '?');
		} else {
			esc_octal(o, c);
		}
	}
}

void name_print(struct dstr *out, const char *s, size_t len, int mb_cur_max, int flags)
{
	if (flags & NP_NOPRINT) { /* -N: raw */
		if (flags & NP_QUOTE)
			dstr_appendc(out, '"');
		dstr_append(out, s, len);
		if (flags & NP_QUOTE)
			dstr_appendc(out, '"');
		return;
	}

	if (mb_cur_max > 1) {
		wchar_t *ws = asp_xmalloc((len + 1) * sizeof *ws);
		size_t k = mbstowcs(ws, s, len + 1);
		if (k != (size_t)-1) {
			char mb[MB_LEN_MAX];
			if (flags & NP_QUOTE)
				dstr_appendc(out, '"');
			(void)wctomb(NULL, 0); /* reset shift state; result unused */
			for (size_t i = 0; i < k; i++) {
				if (iswprint((wint_t)ws[i])) {
					int n = wctomb(mb, ws[i]);
					if (n > 0)
						dstr_append(out, mb, (size_t)n);
				} else if (flags & NP_QMARK) {
					dstr_appendc(out, '?');
				} else {
					esc_octal(out, (unsigned long)ws[i]);
				}
			}
			if (flags & NP_QUOTE)
				dstr_appendc(out, '"');
			free(ws);
			return;
		}
		free(ws);
		/* invalid multibyte sequence -> fall back to byte handling */
	}

	if (flags & NP_QUOTE)
		dstr_appendc(out, '"');
	byte_branch(out, s, len, mb_cur_max, flags);
	if (flags & NP_QUOTE)
		dstr_appendc(out, '"');
}
