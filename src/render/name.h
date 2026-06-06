#ifndef ASP_RENDER_NAME_H
#define ASP_RENDER_NAME_H

/*
 * Name printing — faithful to tree's printit(), including the locale-dependent
 * (wide vs byte) branches and the -q/-N/-Q variants (.docs/audits/01 §14, §9).
 */

#include "dstr.h"

#include <stddef.h>

enum {
	NP_QUOTE   = 1u << 0, /* -Q: wrap in double quotes */
	NP_NOPRINT = 1u << 1, /* -N: print raw, no escaping */
	NP_QMARK   = 1u << 2, /* -q: non-printable -> '?' */
};

void name_print(struct dstr *out, const char *s, size_t len, int mb_cur_max, int flags);

#endif /* ASP_RENDER_NAME_H */
