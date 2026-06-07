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
		char c = s[i];
		unsigned char uc = (unsigned char)c;
		/* Whitelist matches tree: only ASCII alphanumerics and a few unreserved
		 * chars pass through; every high byte is percent-encoded (the <0x80 guard
		 * keeps it locale-independent — macOS isalnum(0xC3) is true in a UTF-8
		 * locale otherwise, leaking the byte un-encoded). */
		if ((uc < 0x80 && isalnum(uc)) || strchr(unreserved, c)) {
			dstr_appendc(out, c);
		} else {
			/* DEVIATION D3: emit the real two-hex-digit byte (%C3). tree formats
			 * the *signed* char with %02X, so a high byte sign-extends to junk
			 * like %FFFFFFC3 — an unresolvable href. See .docs/deviations.md. */
			char b[8];
			int m = snprintf(b, sizeof b, "%%%02X", uc);
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
