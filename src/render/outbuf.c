#include "render/outbuf.h"

#include <errno.h>
#include <unistd.h>

void asp_out_flush(struct dstr *o, int fd)
{
	size_t off = 0;
	while (off < o->len) {
		ssize_t w = write(fd, o->data + off, o->len - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			break; /* output error; nothing graceful to do mid-tree */
		}
		off += (size_t)w;
	}
	dstr_clear(o);
}

void asp_out_maybe(struct dstr *o, int fd)
{
	if (o->len >= ASP_OUT_FLUSH)
		asp_out_flush(o, fd);
}

void asp_out_indent4(struct dstr *o, int level, int noindent)
{
	if (noindent)
		return;
	for (int i = 0; i <= level; i++)
		dstr_appendz(o, "    ");
}
