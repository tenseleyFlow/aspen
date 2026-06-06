#ifndef ASP_RENDER_NAME_H
#define ASP_RENDER_NAME_H

/*
 * Default-mode name printing — faithful to tree's printit() with no -q/-N/-Q.
 * Locale-dependent (this is tree's actual behavior, .docs/audits/01 §14):
 *   - multibyte locale (mb_cur_max > 1): printable wide chars pass through,
 *     non-printable -> \ooo octal. Spaces are printable (not escaped).
 *   - single-byte locale: control 7..13 -> \a\b\t\n\v\f\r, '\\' -> \\,
 *     ' ' -> "\ ", other printable as-is, else \ooo.
 * The -q/-N/-Q variants are added in Sprint 06.
 */

#include "dstr.h"

#include <stddef.h>

void name_print(struct dstr *out, const char *s, size_t len, int mb_cur_max);

#endif /* ASP_RENDER_NAME_H */
