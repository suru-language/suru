#include "test_framework.h"
#include "suru/platform_mem.h"

static void pm_reset(void) { s_pm_dispose_all(); }

static void test_acquire_release(void) {
    pm_reset();
    s_pm_handle h = 0;
    CHECK_EQ_INT((int)s_pm_acquire(64, 0, &h), S_PM_OK);
    CHECK(h != 0);
    CHECK_EQ_INT((int)s_pm_validate(h), S_PM_OK);
    s_size sz = 0;
    s_size al = 0;
    CHECK_EQ_INT((int)s_pm_size(h, &sz), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_align(h, &al), S_PM_OK);
    CHECK_EQ_INT((int)sz, 64);
    CHECK_EQ_INT((int)al, 16);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_validate(h), S_PM_ENOENT);
}

static void test_zero_size(void) {
    pm_reset();
    s_pm_handle h = 0;
    CHECK_EQ_INT((int)s_pm_acquire(0, 0, &h), S_PM_OK);
    CHECK(h != 0);
    s_size sz = 0;
    CHECK_EQ_INT((int)s_pm_size(h, &sz), S_PM_OK);
    CHECK_EQ_INT((int)sz, 0);
    /* zero-length accesses are no-ops and succeed */
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_pm_read(h, 0, &b, 0), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_write(h, 0, &b, 0), S_PM_OK);
    /* non-zero access on empty region must fail */
    CHECK_EQ_INT((int)s_pm_read(h, 0, &b, 1), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_write(h, 0, &b, 1), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
}

static void test_alloc_failure(void) {
    pm_reset();
    s_pm_handle h = 0xDEAD;
    s_pm_set_fail_alloc(true);
    CHECK_EQ_INT((int)s_pm_acquire(64, 0, &h), S_PM_ENOMEM);
    CHECK_EQ_INT((int)h, 0);
    s_pm_set_fail_alloc(false);
    CHECK_EQ_INT((int)s_pm_acquire(16, 0, &h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
}

static void test_alloc_fail_after(void) {
    pm_reset();
    s_pm_handle h = 0;
    s_pm_set_alloc_fail_after(2);
    CHECK_EQ_INT((int)s_pm_acquire(8, 0, &h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_acquire(8, 0, &h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_acquire(8, 0, &h), S_PM_ENOMEM);
    s_pm_set_alloc_fail_after(0);
}

static void test_invalid_handle(void) {
    pm_reset();
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_pm_validate(0), S_PM_ENOENT);
    CHECK_EQ_INT((int)s_pm_read(0, 0, &b, 1), S_PM_ENOENT);
    CHECK_EQ_INT((int)s_pm_write(0, 0, &b, 1), S_PM_ENOENT);
    CHECK_EQ_INT((int)s_pm_release(0), S_PM_ENOENT);
    /* handle beyond registry */
    CHECK_EQ_INT((int)s_pm_validate(0xFFFF), S_PM_ENOENT);
}

static void test_stale_handle(void) {
    pm_reset();
    s_pm_handle h = 0;
    CHECK_EQ_INT((int)s_pm_acquire(32, 0, &h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_pm_read(h, 0, &b, 1), S_PM_ENOENT);
    CHECK_EQ_INT((int)s_pm_write(h, 0, &b, 1), S_PM_ENOENT);
    CHECK_EQ_INT((int)s_pm_validate(h), S_PM_ENOENT);
}

static void test_double_release(void) {
    pm_reset();
    s_pm_handle h = 0;
    CHECK_EQ_INT((int)s_pm_acquire(32, 0, &h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_ENOENT);
}

static void test_read_write_bounds(void) {
    pm_reset();
    s_pm_handle h = 0;
    CHECK_EQ_INT((int)s_pm_acquire(16, 0, &h), S_PM_OK);
    /* write at start and final permitted byte */
    CHECK_EQ_INT((int)s_pm_write_u8(h, 0, 0xAA), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_write_u8(h, 15, 0xBB), S_PM_OK);
    unsigned char a = 0, b = 0;
    CHECK_EQ_INT((int)s_pm_read_u8(h, 0, &a), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_read_u8(h, 15, &b), S_PM_OK);
    CHECK_EQ_INT((int)a, 0xAA);
    CHECK_EQ_INT((int)b, 0xBB);
    /* u32 aligned write/read */
    CHECK_EQ_INT((int)s_pm_write_u32(h, 4, 0x11223344), S_PM_OK);
    s_u32 u = 0;
    CHECK_EQ_INT((int)s_pm_read_u32(h, 4, &u), S_PM_OK);
    CHECK_EQ_INT((int)u, 0x11223344);
    /* out of bounds read/write */
    CHECK_EQ_INT((int)s_pm_read(h, 16, &a, 1), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_write(h, 16, &a, 1), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_read(h, 15, &a, 2), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_write(h, 15, &a, 2), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_read_u32(h, 13, &u), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
}

static void test_overflow_arithmetic(void) {
    pm_reset();
    s_pm_handle h = 0;
    /* Request an impossibly large region -> overflow. */
    CHECK_EQ_INT((int)s_pm_acquire((s_size)-1, 16, &h), S_PM_EOVERFLOW);
    CHECK_EQ_INT((int)h, 0);
    /* Valid small region, then access with huge offset/len. */
    CHECK_EQ_INT((int)s_pm_acquire(8, 0, &h), S_PM_OK);
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_pm_read(h, (s_size)-1, &b, 1), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_write(h, (s_size)-1, &b, 1), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_read(h, 0, &b, (s_size)-1), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_write(h, 0, &b, (s_size)-1), S_PM_EOOB);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
}

static void test_invalid_alignment(void) {
    pm_reset();
    s_pm_handle h = 7;
    CHECK_EQ_INT((int)s_pm_acquire(32, 3, &h), S_PM_EINVAL); /* not pow2 */
    CHECK_EQ_INT((int)h, 0);
    CHECK_EQ_INT((int)s_pm_acquire(32, 7, &h), S_PM_EINVAL);
    CHECK_EQ_INT((int)h, 0);
}

static void test_repeated_cycles(void) {
    pm_reset();
    s_pm_handle h = 0;
    int i;
    for (i = 0; i < 200; ++i) {
        CHECK_EQ_INT((int)s_pm_acquire(((s_size)(i % 64)) + 1, 0, &h), S_PM_OK);
        CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
    }
    CHECK_EQ_INT((int)s_pm_active_count(), 0);
}

static void test_cleanup_and_leak(void) {
    pm_reset();
    s_pm_handle h1 = 0, h2 = 0, h3 = 0;
    CHECK_EQ_INT((int)s_pm_acquire(16, 0, &h1), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_acquire(32, 0, &h2), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_acquire(64, 0, &h3), S_PM_OK);
    /* release one; dispose_all must free the rest with no leak */
    CHECK_EQ_INT((int)s_pm_release(h2), S_PM_OK);
    s_pm_dispose_all();
    CHECK_EQ_INT((int)s_pm_active_count(), 0);
}

static void test_zero_len_invalid_handle(void) {
    pm_reset();
    s_pm_handle h = 0;
    CHECK_EQ_INT((int)s_pm_acquire(8, 0, &h), S_PM_OK);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
    unsigned char b = 0;
    /* zero-length access on a released handle is still invalid */
    CHECK_EQ_INT((int)s_pm_read(h, 0, &b, 0), S_PM_ENOENT);
    CHECK_EQ_INT((int)s_pm_write(h, 0, &b, 0), S_PM_ENOENT);
}

static void test_alignment_reported(void) {
    pm_reset();
    s_pm_handle h = 0;
    CHECK_EQ_INT((int)s_pm_acquire(40, 64, &h), S_PM_OK);
    s_size al = 0;
    CHECK_EQ_INT((int)s_pm_align(h, &al), S_PM_OK);
    CHECK_EQ_INT((int)al, 64);
    CHECK_EQ_INT((int)s_pm_release(h), S_PM_OK);
}

TEST_MAIN_BEGIN()
    test_acquire_release();
    test_zero_size();
    test_alloc_failure();
    test_alloc_fail_after();
    test_invalid_handle();
    test_stale_handle();
    test_double_release();
    test_read_write_bounds();
    test_overflow_arithmetic();
    test_invalid_alignment();
    test_repeated_cycles();
    test_cleanup_and_leak();
    test_zero_len_invalid_handle();
    test_alignment_reported();
TEST_MAIN_END()
