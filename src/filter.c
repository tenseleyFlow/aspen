#include "filter.h"
#include "glob.h"
#include "util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Trim a .gitignore line: drop trailing \r\n, then trailing unescaped spaces,
 * then unescape '\'. In place. (tree's gittrim.) */
void asp_gittrim(char *s)
{
	ssize_t i, e = (ssize_t)strlen(s) - 1;
	if (e < 0)
		return;
	while (e > 0 && (s[e] == '\n' || s[e] == '\r'))
		e--;
	for (i = e; i >= 0; i--) {
		if (s[i] != ' ')
			break;
		if (i && s[i - 1] != '\\')
			e--;
	}
	s[e + 1] = '\0';
	/* Unescape '\'. NB: a line ending in a lone backslash makes this scan one byte
	 * PAST the logical terminator (the `i++` skips the '\0', then s[i++] reads the
	 * next byte). This is tree's own gittrim behaviour (SR02-0.8 / audit L5): it
	 * stays within the caller's fgets'd PATH_MAX buffer — which is always
	 * NUL-terminated from a prior read, so the scan halts in-bounds (ASan-clean) —
	 * and, because aspen and tree share the same fixed-buffer fgets reuse, both
	 * pick up the identical stale bytes and emit byte-identical output (verified on
	 * trailing-backslash .gitignore fixtures). Reproduced deliberately, NOT a
	 * deviation: "fixing" it would stop at the NUL and diverge from tree. */
	for (i = e = 0; s[i] != '\0';) {
		if (s[i] == '\\')
			i++;
		s[e++] = s[i++];
	}
	s[e] = '\0';
}

struct gpattern *asp_new_gpattern(const char *pattern)
{
	struct gpattern *p = asp_xmalloc(sizeof *p);
	const char *sl = strchr(pattern, '/');
	size_t off = (pattern[0] == '/') ? 1 : 0;
	size_t n = strlen(pattern + off) + 1;
	p->pattern = asp_xmalloc(n);
	memcpy(p->pattern, pattern + off, n);
	p->relative = (sl == NULL || !*(sl + 1));
	p->next = NULL;
	return p;
}

static int is_file(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static struct ignorefile *parse(const char *basepath, FILE *fp)
{
	char buf[PATH_MAX];
	struct gpattern *rmlist = NULL, *remend = NULL, *reverse = NULL, *revend = NULL;

	while (fgets(buf, sizeof buf, fp) != NULL) {
		if (buf[0] == '#')
			continue;
		int rev = (buf[0] == '!');
		asp_gittrim(buf);
		if (buf[0] == '\0')
			continue;
		struct gpattern *p = asp_new_gpattern(buf + (rev ? 1 : 0));
		if (rev) {
			if (!reverse) reverse = revend = p;
			else { revend->next = p; revend = p; }
		} else {
			if (!rmlist) rmlist = remend = p;
			else { remend->next = p; remend = p; }
		}
	}

	struct ignorefile *ig = asp_xmalloc(sizeof *ig);
	ig->remove = rmlist;
	ig->reverse = reverse;
	size_t bn = strlen(basepath) + 1;
	ig->path = asp_xmalloc(bn);
	memcpy(ig->path, basepath, bn);
	ig->next = NULL;
	return ig;
}

struct ignorefile *gitignore_load_dir(const char *dirpath)
{
	char buf[PATH_MAX];
	snprintf(buf, sizeof buf, "%s/.gitignore", dirpath);
	if (!is_file(buf))
		return NULL;
	FILE *fp = fopen(buf, "r");
	if (!fp)
		return NULL;
	struct ignorefile *ig = parse(dirpath, fp);
	fclose(fp);
	return ig;
}

struct ignorefile *gitignore_load_file(const char *basepath, const char *filepath)
{
	FILE *fp = fopen(filepath, "r");
	if (!fp)
		return NULL;
	struct ignorefile *ig = parse(basepath, fp);
	fclose(fp);
	return ig;
}

void gitstack_push(struct ignorefile **stack, struct ignorefile *ig)
{
	if (!ig)
		return;
	ig->next = *stack;
	*stack = ig;
}

void asp_free_gpatterns(struct gpattern *p)
{
	while (p) {
		struct gpattern *n = p->next;
		free(p->pattern);
		free(p);
		p = n;
	}
}

void gitstack_pop(struct ignorefile **stack)
{
	struct ignorefile *ig = *stack;
	if (!ig)
		return;
	*stack = ig->next;
	asp_free_gpatterns(ig->remove);
	asp_free_gpatterns(ig->reverse);
	free(ig->path);
	free(ig);
}

void gitstack_flush(struct ignorefile **stack)
{
	while (*stack)
		gitstack_pop(stack);
}

/* gitignore uses an exact (==1) patmatch, unlike -P's truthy test. */
static int match(struct gpattern *p, const char *path, const char *name, int isdir,
		 int ic, const char *base)
{
	if (p->relative)
		return asp_patmatch(name, p->pattern, isdir, ic) == 1;
	char buf[PATH_MAX * 2];
	snprintf(buf, sizeof buf, "%s/%s", base, p->pattern);
	return asp_patmatch(path, buf, isdir, ic) == 1;
}

int gitignore_filtered(struct ignorefile *stack, const char *path,
		       const char *name, int isdir, int ic)
{
	int filter = 0;
	for (struct ignorefile *ig = stack; !filter && ig; ig = ig->next)
		for (struct gpattern *p = ig->remove; p; p = p->next)
			if (match(p, path, name, isdir, ic, ig->path)) {
				filter = 1;
				break;
			}
	if (!filter)
		return 0;

	for (struct ignorefile *ig = stack; ig; ig = ig->next)
		for (struct gpattern *p = ig->reverse; p; p = p->next)
			if (match(p, path, name, isdir, ic, ig->path))
				return 0; /* re-included by a ! rule */
	return 1;
}
