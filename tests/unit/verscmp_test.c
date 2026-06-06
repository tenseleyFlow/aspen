#include "test.h"
#include "verscmp.h"

int main(void)
{
	CHECK("equal strings", asp_verscmp("no digit", "no digit") == 0);
	CHECK("99 < 100", asp_verscmp("item#99", "item#100") < 0);
	CHECK("alpha1 > alpha001", asp_verscmp("alpha1", "alpha001") > 0);
	CHECK("f012 > f01", asp_verscmp("part1_f012", "part1_f01") > 0);
	CHECK("foo.009 < foo.0", asp_verscmp("foo.009", "foo.0") < 0);
	CHECK("file2 < file10", asp_verscmp("file2", "file10") < 0);
	CHECK("file10 < file100", asp_verscmp("file10", "file100") < 0);
	CHECK("v1.9 < v1.10", asp_verscmp("v1.9", "v1.10") < 0);
	return test_summary("verscmp");
}
