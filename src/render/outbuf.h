#ifndef ASP_RENDER_OUTBUF_H
#define ASP_RENDER_OUTBUF_H

/* Shared output-buffer helpers. Every renderer builds into a dstr and drains it
 * to an fd in 64 KB batches (no per-line write()); these centralize the
 * identical flush/maybe-flush each one used to carry, plus the JSON/XML 4-space
 * indent. See dstr.[ch]. */

#include "dstr.h"

#define ASP_OUT_FLUSH (64u * 1024u) /* drain threshold */

/* Write the whole buffer to fd (EINTR-safe) and clear it. */
void asp_out_flush(struct dstr *o, int fd);

/* Flush once the buffer reaches the drain threshold (hot-loop friendly). */
void asp_out_maybe(struct dstr *o, int fd);

/* JSON/XML indent: (level+1) x 4 spaces, suppressed under --noindent. */
void asp_out_indent4(struct dstr *o, int level, int noindent);

#endif /* ASP_RENDER_OUTBUF_H */
