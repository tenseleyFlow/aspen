#include "test.h"
#include "dstr.h"

int main(void)
{
	struct dstr s;
	dstr_init(&s);

	dstr_appendz(&s, "hello");
	CHECK_STR("appendz", s.data, "hello");
	CHECK_SZ("len after appendz", s.len, 5);

	dstr_appendc(&s, ' ');
	dstr_appendz(&s, "world");
	CHECK_STR("concat", s.data, "hello world");
	CHECK_SZ("len after concat", s.len, 11);

	dstr_append(&s, "\0!", 2); /* embedded NUL preserved by length */
	CHECK_SZ("len with embedded NUL", s.len, 13);
	CHECK("NUL terminated", s.data[s.len] == '\0');

	dstr_clear(&s);
	CHECK_SZ("clear len", s.len, 0);
	CHECK_STR("clear empty", s.data, "");

	for (int i = 0; i < 10000; i++)
		dstr_appendc(&s, 'a');
	CHECK_SZ("grown len", s.len, 10000);
	CHECK("cap covers len+NUL", s.cap >= 10001);

	dstr_free(&s);
	return test_summary("dstr");
}
