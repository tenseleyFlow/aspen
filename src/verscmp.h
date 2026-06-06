#ifndef ASP_VERSCMP_H
#define ASP_VERSCMP_H

/* Version-aware string compare (glibc strverscmp semantics), ported from tree's
 * strverscmp.c so behavior is identical on glibc/musl/BSD/macOS alike. */
int asp_verscmp(const char *s1, const char *s2);

#endif /* ASP_VERSCMP_H */
