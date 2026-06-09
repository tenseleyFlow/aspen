#ifndef ASP_RENDER_XML_H
#define ASP_RENDER_XML_H

/* XML output (-X), faithful to tree's xml.c (uses the renderer tree hook). */

#include "dstr.h"
#include "options.h"
#include "render.h"
#include "sort.h"

struct xml_ctx {
	struct dstr out;
	int fd;
	const struct options *o;
	struct dstr fp; /* -f only: path stack (root + "/name" per level) for full names */
	struct asp_sort_scratch sortscr; /* reused by the per-level asp_sort on emit */
};

void xml_ctx_init(struct xml_ctx *x, int fd, const struct options *o);
void xml_ctx_destroy(struct xml_ctx *x);

extern const struct renderer asp_xml_renderer;

#endif /* ASP_RENDER_XML_H */
