#include "test.h"
#include "util.h"

int main(void)
{
	CHECK_SZ("add", asp_size_add(2, 3), 5);
	CHECK("add saturates", asp_size_add((size_t)-1, 1) == (size_t)-1);
	CHECK_SZ("mul", asp_size_mul(4, 5), 20);
	CHECK("mul saturates", asp_size_mul((size_t)-1, 2) == (size_t)-1);
	CHECK_SZ("mul by zero", asp_size_mul(0, 5), 0);

	CHECK_SZ("bit_width(0)", asp_bit_width(0), 0);
	CHECK_SZ("bit_width(1)", asp_bit_width(1), 1);
	CHECK_SZ("bit_width(255)", asp_bit_width(255), 8);
	CHECK_SZ("bit_width(256)", asp_bit_width(256), 9);

	CHECK_SZ("bit_ceil(0)", asp_bit_ceil(0), 1);
	CHECK_SZ("bit_ceil(1)", asp_bit_ceil(1), 1);
	CHECK_SZ("bit_ceil(3)", asp_bit_ceil(3), 4);
	CHECK_SZ("bit_ceil(4)", asp_bit_ceil(4), 4);
	CHECK_SZ("bit_ceil(5)", asp_bit_ceil(5), 8);
	CHECK_SZ("bit_ceil(1000)", asp_bit_ceil(1000), 1024);

	return test_summary("util");
}
