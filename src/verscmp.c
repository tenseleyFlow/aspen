/* Version-aware string comparison — ported from GNU libiberty / tree's
 * strverscmp.c (LGPL-2.1+, FSF). Renamed asp_verscmp and used on all platforms
 * so version sort (-v / --sort=version) is byte-identical everywhere. */
#include "verscmp.h"

#include <ctype.h>

#define S_N 0x0
#define S_I 0x4
#define S_F 0x8
#define S_Z 0xC
#define CMP 2
#define LEN 3

int asp_verscmp(const char *s1, const char *s2)
{
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	unsigned char c1, c2;
	int state;
	int diff;

	static const unsigned int next_state[] = {
		/* state    x    d    0    - */
		/* S_N */ S_N, S_I, S_Z, S_N,
		/* S_I */ S_N, S_I, S_I, S_I,
		/* S_F */ S_N, S_F, S_F, S_F,
		/* S_Z */ S_N, S_F, S_Z, S_Z
	};
	static const int result_type[] = {
		/* S_N */ CMP, CMP, CMP, CMP, CMP, LEN, CMP, CMP,
		CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
		/* S_I */ CMP, -1, -1, CMP, +1, LEN, LEN, CMP,
		+1, LEN, LEN, CMP, CMP, CMP, CMP, CMP,
		/* S_F */ CMP, CMP, CMP, CMP, CMP, LEN, CMP, CMP,
		CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
		/* S_Z */ CMP, +1, +1, CMP, -1, CMP, CMP, CMP,
		-1, CMP, CMP, CMP
	};

	if (p1 == p2)
		return 0;

	c1 = *p1++;
	c2 = *p2++;
	state = S_N | ((c1 == '0') + (isdigit(c1) != 0));

	while ((diff = c1 - c2) == 0 && c1 != '\0') {
		state = (int)next_state[state];
		c1 = *p1++;
		c2 = *p2++;
		state |= (c1 == '0') + (isdigit(c1) != 0);
	}

	state = result_type[state << 2 | (((c2 == '0') + (isdigit(c2) != 0)))];

	switch (state) {
	case CMP:
		return diff;
	case LEN:
		while (isdigit(*p1++))
			if (!isdigit(*p2++))
				return 1;
		return isdigit(*p2) ? -1 : diff;
	default:
		return state;
	}
}
