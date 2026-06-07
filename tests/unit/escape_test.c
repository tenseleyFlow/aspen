#include "test.h"
#include "dstr.h"
#include "render/escape.h"

static const char *enc(struct dstr *s, const char *in)
{
	dstr_clear(s);
	asp_url_encode(s, in);
	return s->data;
}

int main(void)
{
	struct dstr s;
	dstr_init(&s);

	/* whitelist: ASCII alnum and /-._~ pass through verbatim */
	CHECK_STR("alnum passthrough", enc(&s, "Abc123"), "Abc123");
	CHECK_STR("unreserved passthrough", enc(&s, "a/b-c._d~e"), "a/b-c._d~e");

	/* other ASCII is percent-encoded with uppercase hex */
	CHECK_STR("space encoded", enc(&s, "a b"), "a%20b");
	CHECK_STR("percent encoded", enc(&s, "100%"), "100%25");

	/* DEVIATION D3: a high byte encodes to its REAL two-hex-digit value, not
	 * tree's sign-extended %FFFFFFxx. "café" = 63 61 66 C3 A9. */
	CHECK_STR("utf8 high byte D3", enc(&s, "caf\xc3\xa9"), "caf%C3%A9");
	CHECK_STR("0xFF byte", enc(&s, "\xff"), "%FF");
	CHECK("no sign extension", strstr(enc(&s, "\xc3"), "FFFF") == NULL);

	/* return value reports a trailing slash (used to suppress a dir's extra '/') */
	dstr_clear(&s);
	CHECK("trailing slash -> 1", asp_url_encode(&s, "a/") == 1);
	dstr_clear(&s);
	CHECK("no trailing slash -> 0", asp_url_encode(&s, "ab") == 0);

	/* html_encode: the five metacharacters become entities, rest verbatim */
	dstr_clear(&s); asp_html_encode(&s, "a<b>&\"c");
	CHECK_STR("html entities", s.data, "a&lt;b&gt;&amp;&quot;c");

	dstr_free(&s);
	return test_summary("escape");
}
