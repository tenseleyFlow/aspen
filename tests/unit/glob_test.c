#include "test.h"
#include "glob.h"

#include <string.h>

/* patmatch may rewrite pat for '|'; give it a writable copy. */
static int m(const char *name, const char *pat, int isdir, int ic)
{
	char buf[256];
	strncpy(buf, pat, sizeof buf - 1);
	buf[sizeof buf - 1] = '\0';
	return asp_patmatch(name, buf, isdir, ic);
}

int main(void)
{
	CHECK("literal", m("foo.txt", "foo.txt", 0, 0) == 1);
	CHECK("star match", m("foo.txt", "*.txt", 0, 0) == 1);
	CHECK("star mismatch", m("foo.log", "*.txt", 0, 0) == 0);
	CHECK("star stops at /", m("a/b", "*", 0, 0) == 0);
	CHECK("starstar crosses /", m("a/b", "**", 0, 0) == 1);
	CHECK("question", m("ab", "a?", 0, 0) == 1);
	CHECK("class", m("a", "[abc]", 0, 0) == 1);
	CHECK("class neg hit", m("d", "[^abc]", 0, 0) == 1);
	CHECK("class neg miss", m("a", "[^abc]", 0, 0) == 0);
	CHECK("range", m("c", "[a-z]", 0, 0) == 1);
	CHECK("alt left", m("foo", "foo|bar", 0, 0) == 1);
	CHECK("alt right", m("bar", "foo|bar", 0, 0) == 1);
	CHECK("alt none", m("baz", "foo|bar", 0, 0) == 0);
	CHECK("ignore-case", m("FOO", "foo", 0, 1) == 1);
	CHECK("dironly vs file", m("d", "d/", 0, 0) == 0);
	CHECK("dironly vs dir", m("d", "d/", 1, 0) == 1);
	CHECK("syntax error -> -1", m("x", "abc|", 0, 0) == -1);
	return test_summary("glob");
}
