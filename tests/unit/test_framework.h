#ifndef SURU_TEST_FRAMEWORK_H
#define SURU_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int su_test_failures = 0;
static int su_test_checks = 0;

#define CHECK(cond) do {                                                    \
    su_test_checks++;                                                       \
    if (!(cond)) {                                                          \
        su_test_failures++;                                                 \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n",                      \
                __FILE__, __LINE__, #cond);                                 \
    }                                                                       \
} while (0)

#define CHECK_EQ_INT(a, b) do {                                             \
    su_test_checks++;                                                       \
    long long _a = (long long)(a), _b = (long long)(b);                     \
    if (_a != _b) {                                                         \
        su_test_failures++;                                                 \
        fprintf(stderr, "CHECK_EQ_INT failed at %s:%d: %lld != %lld (%s vs %s)\n", \
                __FILE__, __LINE__, _a, _b, #a, #b);                       \
    }                                                                       \
} while (0)

#define CHECK_STR_EQ(a, b) do {                                             \
    su_test_checks++;                                                       \
    const char *_a = (a), *_b = (b);                                        \
    if (!_a || !_b || strcmp(_a, _b) != 0) {                               \
        su_test_failures++;                                                 \
        fprintf(stderr, "CHECK_STR_EQ failed at %s:%d: \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); \
    }                                                                       \
} while (0)

#define TEST_MAIN_BEGIN() int main(void) {
#define TEST_MAIN_END()                                                    \
    if (su_test_failures) {                                                 \
        fprintf(stderr, "FAILED: %d/%d checks failed\n",                    \
                su_test_failures, su_test_checks);                          \
        return 1;                                                           \
    }                                                                       \
    fprintf(stderr, "OK: %d checks passed\n", su_test_checks);              \
    return 0;                                                               \
}

#endif /* SURU_TEST_FRAMEWORK_H */
