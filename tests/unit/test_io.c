#include "test_framework.h"
#include "suru/io.h"

static void test_binary_round_trip(void) {
    const char *path = "build/unit/suru-io-test.bin";
    const s_u8 input[] = { 0x00, 0x53, 0x55, 0x52, 0x55, 0xff, 0x00 };
    s_u8 *output = NULL;
    s_size output_len = 0;

    CHECK_EQ_INT(s_io_write_all(path, input, sizeof input), 0);
    CHECK_EQ_INT(s_io_read_all(path, &output, &output_len, 1024), 0);
    CHECK_EQ_INT(output_len, sizeof input);
    CHECK(output != NULL && memcmp(output, input, sizeof input) == 0);
    free(output);
    (void)remove(path);
}

static void test_limits_and_errors(void) {
    const char *path = "build/unit/suru-io-limit.bin";
    const s_u8 input[] = { 1, 2, 3, 4 };
    s_u8 *output = NULL;
    s_size output_len = 0;

    CHECK_EQ_INT(s_io_write_all(path, input, sizeof input), 0);
    CHECK(s_io_read_all(path, &output, &output_len, 3) != 0);
    CHECK(output == NULL);
    CHECK_EQ_INT(output_len, 0);
    CHECK(s_io_read_all("build/unit/does-not-exist", &output, &output_len, 0) != 0);
    (void)remove(path);
}

TEST_MAIN_BEGIN()
    test_binary_round_trip();
    test_limits_and_errors();
TEST_MAIN_END()
