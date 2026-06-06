#include "info.h"
#include "filter.h"
#include "glob.h"
#include "util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_line(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = asp_xmalloc(n);
	memcpy(d, s, n);
	return d;
}

static struct icomment *new_comment(struct gpattern *phead, char **line, int lines)
{
	struct icomment *com = asp_xmalloc(sizeof *com);
	com->pattern = phead;
	com->desc = asp_xmalloc(sizeof(char *) * (size_t)(lines + 1));
	for (int i = 0; i < lines; i++)
		com->desc[i] = line[i];
	com->desc[lines] = NULL;
	com->next = NULL;
	return com;
}

static struct infofile *parse(const char *basepath, FILE *fp)
{
	char buf[PATH_MAX];
	char *line[PATH_MAX];
	int lines = 0;
	struct icomment *chead = NULL, *cend = NULL;
	struct gpattern *phead = NULL, *pend = NULL;

	while (fgets(buf, sizeof buf, fp) != NULL) {
		if (buf[0] == '#')
			continue;
		asp_gittrim(buf);
		if (buf[0] == '\0')
			continue;

		if (buf[0] == '\t') {
			if (lines < PATH_MAX)
				line[lines++] = dup_line(buf + 1);
		} else {
			if (lines) {
				if (phead) {
					struct icomment *com = new_comment(phead, line, lines);
					if (!chead) chead = cend = com;
					else cend = cend->next = com;
				} else {
					for (int i = 0; i < lines; i++)
						free(line[i]);
				}
				phead = pend = NULL;
				lines = 0;
			}
			struct gpattern *p = asp_new_gpattern(buf);
			if (!phead) phead = pend = p;
			else pend = pend->next = p;
		}
	}
	if (phead) {
		struct icomment *com = new_comment(phead, line, lines);
		if (!chead) chead = cend = com;
		else cend = cend->next = com;
	} else {
		for (int i = 0; i < lines; i++)
			free(line[i]);
	}

	struct infofile *inf = asp_xmalloc(sizeof *inf);
	inf->comments = chead;
	size_t bn = strlen(basepath) + 1;
	inf->path = asp_xmalloc(bn);
	memcpy(inf->path, basepath, bn);
	inf->next = NULL;
	return inf;
}

struct infofile *info_load_dir(const char *dirpath)
{
	char buf[PATH_MAX];
	snprintf(buf, sizeof buf, "%s/.info", dirpath);
	FILE *fp = fopen(buf, "r");
	if (!fp)
		return NULL;
	struct infofile *inf = parse(dirpath, fp);
	fclose(fp);
	return inf;
}

struct infofile *info_load_file(const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp)
		return NULL;
	struct infofile *inf = parse(path, fp);
	fclose(fp);
	return inf;
}

void infostack_push(struct infofile **stack, struct infofile *inf)
{
	if (!inf)
		return;
	inf->next = *stack;
	*stack = inf;
}

void infostack_pop(struct infofile **stack)
{
	struct infofile *inf = *stack;
	if (!inf)
		return;
	*stack = inf->next;
	for (struct icomment *c = inf->comments; c;) {
		struct icomment *cn = c->next;
		asp_free_gpatterns(c->pattern);
		for (int i = 0; c->desc[i]; i++)
			free(c->desc[i]);
		free(c->desc);
		free(c);
		c = cn;
	}
	free(inf->path);
	free(inf);
}

void infostack_flush(struct infofile **stack)
{
	while (*stack)
		infostack_pop(stack);
}

char **info_check(struct infofile *stack, const char *path, const char *name,
		  int top, int isdir, int ic)
{
	for (struct infofile *inf = stack; inf; inf = inf->next) {
		char abs[PATH_MAX * 2];
		for (struct icomment *com = inf->comments; com; com = com->next) {
			for (struct gpattern *p = com->pattern; p; p = p->next) {
				if (asp_patmatch(path, p->pattern, isdir, ic) == 1)
					return com->desc;
				if (top && asp_patmatch(name, p->pattern, isdir, ic) == 1)
					return com->desc;
				snprintf(abs, sizeof abs, "%s/%s", inf->path, p->pattern);
				if (asp_patmatch(path, abs, isdir, ic) == 1)
					return com->desc;
			}
		}
		top = 0;
	}
	return NULL;
}
