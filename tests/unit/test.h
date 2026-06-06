#ifndef ASP_TEST_H
#define ASP_TEST_H

/* Dependency-free unit harness. One test binary per .c file; each writes its
 * own main(), uses CHECK_* macros, and ends with return test_summary("name"). */

#include <stdio.h>
#include <string.h>

static int t_checks;
static int t_fails;

#define CHECK(msg, cond)                                                        \
	do {                                                                    \
		t_checks++;                                                     \
		if (!(cond)) {                                                  \
			t_fails++;                                              \
			fprintf(stderr, "  FAIL %s (%s:%d): %s\n", (msg),       \
				__FILE__, __LINE__, #cond);                     \
		}                                                               \
	} while (0)

#define CHECK_SZ(msg, got, want)                                                \
	do {                                                                    \
		t_checks++;                                                     \
		size_t g_ = (got), w_ = (want);                                 \
		if (g_ != w_) {                                                 \
			t_fails++;                                              \
			fprintf(stderr, "  FAIL %s (%s:%d): got=%zu want=%zu\n", \
				(msg), __FILE__, __LINE__, g_, w_);             \
		}                                                               \
	} while (0)

#define CHECK_STR(msg, got, want)                                               \
	do {                                                                    \
		t_checks++;                                                     \
		const char *g_ = (got), *w_ = (want);                           \
		if (strcmp(g_, w_) != 0) {                                      \
			t_fails++;                                              \
			fprintf(stderr,                                        \
				"  FAIL %s (%s:%d): got=\"%s\" want=\"%s\"\n",   \
				(msg), __FILE__, __LINE__, g_, w_);             \
		}                                                               \
	} while (0)

static inline int test_summary(const char *label)
{
	fprintf(stderr, "%s: %d checks, %d failed\n", label, t_checks, t_fails);
	return t_fails ? 1 : 0;
}

#endif /* ASP_TEST_H */
