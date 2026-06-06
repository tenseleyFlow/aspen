#include "render/escape.h"
#include "dstr.h"

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
