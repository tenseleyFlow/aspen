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
	/* SR02-1.3: bulk-copy the leading run of "clean" bytes (printable and not
	 * escaped: not '\\', not a quote under -Q, not a space when unquoted; control
	 * bytes 7..13 are already !isprint) in one dstr_append, then fall into the
	 * byte-by-byte loop only from the first byte that needs special handling. The
	 * vast majority of names are entirely clean -> one scan + one memcpy. */
	size_t a = 0;
	while (a < len) {
		unsigned char c = (unsigned char)s[a];
		if (!isprint(c) || c == '\\' || (c == '"' && quote) || (c == ' ' && !quote))
			break;
		a++;
	}
	dstr_append(o, s, a);
	for (size_t i = a; i < len; i++) {
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
		/* Pure-ASCII fast path: in a multibyte locale every ASCII byte is its own
		 * character, so iswprint == isprint and wctomb is the identity — the
		 * result is byte-identical to the wide path below, without the per-name
		 * malloc + mbstowcs + wctomb loop. Only real multibyte names need it. */
		size_t a = 0;
		while (a < len && (unsigned char)s[a] < 0x80)
			a++;
		if (a == len) {
			if (flags & NP_QUOTE)
				dstr_appendc(out, '"');
			for (size_t i = 0; i < len; i++) {
				unsigned char c = (unsigned char)s[i];
				if (isprint(c))
					dstr_appendc(out, (char)c);
				else if (flags & NP_QMARK)
					dstr_appendc(out, '?');
				else
					esc_octal(out, c);
			}
			if (flags & NP_QUOTE)
				dstr_appendc(out, '"');
			return;
		}

		wchar_t *ws = asp_xmalloc((len + 1) * sizeof *ws);
		size_t k = mbstowcs(ws, s, len + 1);
		if (k != (size_t)-1) {
			char mb[MB_LEN_MAX];
			if (flags & NP_QUOTE)
				dstr_appendc(out, '"');
			/* reset shift state; gcc's warn_unused_result ignores a
			 * (void) cast, so consume the value in a condition instead. */
			if (wctomb(NULL, 0)) { /* stateful encoding — nothing to do */ }
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
