#include "test.h"
#include "sys/dir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Build a temp tree, then verify the dir reader: . / .. skipped, d_type mapped,
 * dotfiles still returned (filtering is traverse's job), big dir not truncated. */
int main(void)
{
	mkdir("tests/.work", 0777);
	char tmpl[] = "tests/.work/dirtestXXXXXX";
	char *root = mkdtemp(tmpl);
	CHECK("mkdtemp", root != NULL);
	if (!root)
		return test_summary("dir");

	char p[512];
	snprintf(p, sizeof p, "%s/subdir", root);
	mkdir(p, 0777);
	snprintf(p, sizeof p, "%s/a.txt", root);
	FILE *f = fopen(p, "w");
	if (f) fclose(f);
	snprintf(p, sizeof p, "%s/.hidden", root);
	f = fopen(p, "w");
	if (f) fclose(f);
	snprintf(p, sizeof p, "%s/lnk", root);
	(void)symlink("a.txt", p);

	const int N = 3000;
	for (int i = 0; i < N; i++) {
		snprintf(p, sizeof p, "%s/f%04d", root, i);
		f = fopen(p, "w");
		if (f) fclose(f);
	}

	struct asp_dir *d;
	CHECK("diropen", asp_diropen(root, &d) == 0);

	struct asp_dirent e;
	int count = 0, dotdot = 0, sawhidden = 0;
	enum asp_type t_atxt = ASP_UNKNOWN, t_sub = ASP_UNKNOWN, t_lnk = ASP_UNKNOWN;
	int r;
	while ((r = asp_dirread(d, &e)) == 1) {
		count++;
		if (!strcmp(e.name, ".") || !strcmp(e.name, ".."))
			dotdot++;
		if (!strcmp(e.name, ".hidden"))
			sawhidden = 1;
		if (!strcmp(e.name, "a.txt"))
			t_atxt = e.type;
		if (!strcmp(e.name, "subdir"))
			t_sub = e.type;
		if (!strcmp(e.name, "lnk"))
			t_lnk = e.type;
	}
	CHECK("read ok", r == 0);
	asp_dirclose(d);

	CHECK(". and .. skipped", dotdot == 0);
	CHECK("dotfile returned by reader", sawhidden == 1);
	CHECK_SZ("all entries present", (size_t)count, (size_t)(N + 4)); /* subdir,a.txt,.hidden,lnk */

	/* d_type may be UNKNOWN on some filesystems; assert only when populated. */
	if (t_atxt != ASP_UNKNOWN)
		CHECK("a.txt is REG", t_atxt == ASP_REG);
	if (t_sub != ASP_UNKNOWN)
		CHECK("subdir is DIR", t_sub == ASP_DIR);
	if (t_lnk != ASP_UNKNOWN)
		CHECK("lnk is LNK", t_lnk == ASP_LNK);

	return test_summary("dir");
}
