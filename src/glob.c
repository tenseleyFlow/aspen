/* Ported from tree's patmatch() (Thomas Moore; '|' by David MacMahon;
 * case-insensitivity by Jason A. Donenfeld). Behavior must stay identical. */
#include "glob.h"

#include <ctype.h>
#include <string.h>

static char cond_lower(char c, int ic)
{
	return ic ? (char)tolower((unsigned char)c) : c;
}

int asp_patmatch(const char *buf, char *pat, int isdir, int ic)
{
	int match = 1, n;
	char *bar = strchr(pat, '|');
	char m, pprev = 0;

	if (bar) {
		if (bar == pat || !bar[1])
			return -1;
		*bar = '\0';
		match = asp_patmatch(buf, pat, isdir, ic);
		if (!match)
			match = asp_patmatch(buf, bar + 1, isdir, ic);
		*bar = '|';
		return match;
	}

	while (*pat && match) {
		switch (*pat) {
		case '[':
			pat++;
			if (*pat != '^') {
				n = 1;
				match = 0;
			} else {
				pat++;
				n = 0;
			}
			while (*pat != ']') {
				if (*pat == '\\')
					pat++;
				if (!*pat)
					return -1;
				if (pat[1] == '-') {
					m = *pat;
					pat += 2;
					if (*pat == '\\' && *pat)
						pat++;
					if (cond_lower(*buf, ic) >= cond_lower(m, ic) &&
					    cond_lower(*buf, ic) <= cond_lower(*pat, ic))
						match = n;
					if (!*pat)
						pat--;
				} else if (cond_lower(*buf, ic) == cond_lower(*pat, ic)) {
					match = n;
				}
				pat++;
			}
			buf++;
			break;
		case '*':
			pat++;
			if (!*pat) {
				int f = (strchr(buf, '/') == NULL);
				return f;
			}
			match = 0;
			if (*pat == '*') {
				pat++;
				if (!*pat)
					return 1;
				while (*buf && !(match = asp_patmatch(buf, pat, isdir, ic))) {
					if (pprev == '/' && *pat == '/' && *(pat + 1) &&
					    (match = asp_patmatch(buf, pat + 1, isdir, ic)))
						return match;
					buf++;
					while (*buf && *buf != '/')
						buf++;
				}
			} else {
				while (*buf && !(match = asp_patmatch(buf++, pat, isdir, ic)))
					if (*buf == '/')
						break;
			}
			if (!match && (!*buf || *buf == '/'))
				match = asp_patmatch(buf, pat, isdir, ic);
			return match;
		case '?':
			if (!*buf)
				return 0;
			buf++;
			break;
		case '/':
			if (!*(pat + 1) && !*buf)
				return isdir;
			match = (*buf++ == *pat);
			break;
		case '\\':
			if (*pat)
				pat++;
			/* fall through */
		default:
			match = (cond_lower(*buf++, ic) == cond_lower(*pat, ic));
			break;
		}
		pprev = *pat++;
		if (match < 1)
			return match;
	}
	if (!*buf)
		return match;
	return 0;
}
