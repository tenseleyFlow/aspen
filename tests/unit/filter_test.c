#include "test.h"
#include "filter.h"

#include <limits.h>
#include <string.h>

/* asp_gittrim() unit coverage, run under ASan/UBSan by the harness. The headline
 * case is SR02-0.8 / audit-R1: a PATH_MAX-1 .gitignore line ending in a LONE
 * backslash must not read past the caller's char[PATH_MAX] buffer (tree's gittrim
 * does — a real stack-buffer-overflow). Mirror the real caller's buffer exactly. */
int main(void)
{
	char buf[PATH_MAX];

	/* R1: 1022 'a' + a trailing '\' = PATH_MAX-1 bytes, NUL at buf[PATH_MAX-1].
	 * Old code did `i++` past the '\' then read buf[PATH_MAX] (OOB). Must stay in
	 * bounds (ASan-checked) and keep the trailing backslash verbatim. */
	memset(buf, 'a', sizeof buf);
	buf[PATH_MAX - 2] = '\\';
	buf[PATH_MAX - 1] = '\0';
	asp_gittrim(buf);
	CHECK("PATH_MAX-1 trailing backslash: in-bounds + kept",
	      buf[strlen(buf) - 1] == '\\');

	/* a shorter lone backslash, same property */
	strcpy(buf, "\\");
	asp_gittrim(buf);
	CHECK_STR("lone backslash kept", buf, "\\");

	/* normal unescape: an escaped char loses its backslash */
	strcpy(buf, "a\\b");
	asp_gittrim(buf);
	CHECK_STR("unescape mid-line", buf, "ab");

	/* an escaped backslash collapses to one */
	strcpy(buf, "x\\\\y");
	asp_gittrim(buf);
	CHECK_STR("escaped backslash collapses", buf, "x\\y");

	/* trailing unescaped spaces are trimmed; \r\n stripped */
	strcpy(buf, "foo   \r\n");
	asp_gittrim(buf);
	CHECK_STR("trailing spaces + CRLF trimmed", buf, "foo");

	/* a backslash-escaped trailing space is preserved */
	strcpy(buf, "foo\\ ");
	asp_gittrim(buf);
	CHECK_STR("escaped trailing space kept", buf, "foo ");

	return test_summary("filter");
}
