#ifndef ASP_IDCACHE_H
#define ASP_IDCACHE_H

/* uid/gid -> name, cached (tree's hash.c). Numeric string if no passwd/group
 * entry. Returned pointers are stable for the process lifetime. */

#include <sys/types.h>

const char *uidtoname(uid_t uid);
const char *gidtoname(gid_t gid);

#endif /* ASP_IDCACHE_H */
