#ifndef ASP_IDCACHE_H
#define ASP_IDCACHE_H

/* uid/gid -> name, cached (tree's hash.c). Numeric string if no passwd/group
 * entry. Returned pointers are stable for the process lifetime. */

#include <sys/types.h>

const char *uidtoname(uid_t uid);
const char *gidtoname(gid_t gid);

/* Release the caches (clean shutdown / leak-sanitizer cleanliness). The cache is
 * retained for the process lifetime by design — it avoids repeated getpwuid/
 * getgrgid NSS lookups for -u/-g over many files — so this is only for exit. */
void idcache_free(void);

#endif /* ASP_IDCACHE_H */
