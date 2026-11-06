#include "test_framework.h"
#include "suru/objmodel.h"
#include "suru/platform_mem.h"

static void om_reset(void) { s_om_dispose_all(); s_pm_dispose_all(); }

static void test_heap_lifecycle(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    CHECK(h != 0);
    CHECK_EQ_INT((int)s_om_heap_validate(h), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 0);
    CHECK_EQ_INT((int)s_om_live_bytes(h), 0);
    CHECK_EQ_INT((int)s_om_heap_destroy(h), S_OM_OK);
    CHECK_EQ_INT((int)s_om_heap_validate(h), S_OM_ENOENT);
}

static void test_heap_create_invalid(void) {
    om_reset();
    s_om_heap h = 7;
    CHECK_EQ_INT((int)s_om_heap_create(0, 4096, &h), S_OM_EINVAL);
    CHECK_EQ_INT((int)h, 0);
    h = 7;
    CHECK_EQ_INT((int)s_om_heap_create(64, 8, &h), S_OM_EINVAL);
    CHECK_EQ_INT((int)h, 0);
    h = 7;
    CHECK_EQ_INT((int)s_om_heap_create(S_OM_MAX_OBJECTS + 1, 4096, &h),
                 S_OM_EINVAL);
    CHECK_EQ_INT((int)h, 0);
}

static void test_alloc_str_roundtrip(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0;
    const char *msg = "hello suru";
    s_size len = (s_size)strlen(msg);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, (s_u32)len, len, &o), S_OM_OK);
    CHECK(o != 0);
    CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_OK);
    s_obj_kind k;
    CHECK_EQ_INT((int)s_om_kind(h, o, &k), S_OM_OK);
    CHECK_EQ_INT((int)k, S_OBJ_STR);
    s_u32 c = 0;
    CHECK_EQ_INT((int)s_om_count(h, o, &c), S_OM_OK);
    CHECK_EQ_INT((int)c, (int)len);
    s_size sz = 0;
    CHECK_EQ_INT((int)s_om_size(h, o, &sz), S_OM_OK);
    CHECK_EQ_INT((int)(sz % S_OM_ALIGN), 0);
    CHECK((s_size)sz >= S_OM_HEADER_SIZE + len);
    /* default colour is white */
    s_u8 col = 99;
    CHECK_EQ_INT((int)s_om_colour(h, o, &col), S_OM_OK);
    CHECK_EQ_INT((int)col, S_GC_WHITE);
    CHECK_EQ_INT((int)s_om_write(h, o, 0, msg, len), S_OM_OK);
    char buf[64] = {0};
    CHECK_EQ_INT((int)s_om_read(h, o, 0, buf, len), S_OM_OK);
    CHECK_STR_EQ(buf, msg);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_ENOENT);
}

static void test_alloc_array_children(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_ARRAY, 4, 0, &o), S_OM_OK);
    s_obj_kind k;
    CHECK_EQ_INT((int)s_om_kind(h, o, &k), S_OM_OK);
    CHECK_EQ_INT((int)k, S_OBJ_ARRAY);
    s_u32 c = 0;
    CHECK_EQ_INT((int)s_om_count(h, o, &c), S_OM_OK);
    CHECK_EQ_INT((int)c, 4);
    s_size sz = 0;
    CHECK_EQ_INT((int)s_om_size(h, o, &sz), S_OM_OK);
    CHECK((s_size)sz >= S_OM_HEADER_SIZE + 4 * (s_size)sizeof(s_value));
    s_u32 v = 0xDEADBEEFu;
    CHECK_EQ_INT((int)s_om_write_u32(h, o, 2 * (s_size)sizeof(s_value), v),
                 S_OM_OK);
    s_u32 r = 0;
    CHECK_EQ_INT((int)s_om_read_u32(h, o, 2 * (s_size)sizeof(s_value), &r),
                 S_OM_OK);
    CHECK_EQ_INT((int)r, (int)v);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
}

static void test_alloc_all_kinds(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 8192, &h), S_OM_OK);
    s_obj_kind kinds[] = {S_OBJ_STR, S_OBJ_ARRAY, S_OBJ_RECORD,
                          S_OBJ_CLOSURE, S_OBJ_EXCEPTION};
    int i;
    for (i = 0; i < 5; ++i) {
        s_u32 o = 0;
        s_u32 count = (kinds[i] == S_OBJ_STR) ? 0 : 2;
        CHECK_EQ_INT((int)s_om_alloc(h, kinds[i], count, 0, &o), S_OM_OK);
        s_obj_kind k;
        CHECK_EQ_INT((int)s_om_kind(h, o, &k), S_OM_OK);
        CHECK_EQ_INT((int)k, (int)kinds[i]);
    }
}

static void test_invalid_kind(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, (s_obj_kind)99, 0, 0, &o), S_OM_EINVAL);
    CHECK_EQ_INT((int)o, 0);
}

static void test_payload_bounds(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0;
    /* 8-byte string request rounds up to a 16-byte object; the accessible
     * payload capacity is rounded - header = 16 bytes. */
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &o), S_OM_OK);
    s_size sz = 0;
    CHECK_EQ_INT((int)s_om_size(h, o, &sz), S_OM_OK);
    CHECK_EQ_INT((int)sz, S_OM_HEADER_SIZE + 16);
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_om_read(h, o, 0, &b, 1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_read(h, o, 15, &b, 1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_read(h, o, 16, &b, 1), S_OM_EOOB);
    CHECK_EQ_INT((int)s_om_read(h, o, 15, &b, 2), S_OM_EOOB);
    CHECK_EQ_INT((int)s_om_write(h, o, 16, &b, 1), S_OM_EOOB);
    CHECK_EQ_INT((int)s_om_read_u32(h, o, 13, (s_u32 *)&b), S_OM_EOOB);
}

static void test_stale_and_double_free(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 4, 4, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_ENOENT);
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_om_read(h, o, 0, &b, 1), S_OM_ENOENT);
    /* object 0 is never valid; a huge index is out of range */
    CHECK_EQ_INT((int)s_om_validate(h, 0), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, 0xFFFFFFu), S_OM_ENOENT);
}

static void test_invalid_heap_handle(void) {
    om_reset();
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(0, S_OBJ_STR, 1, 1, &o), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(0xDEADu, 1), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_heap_destroy(0), S_OM_ENOENT);
}

static void test_reuse_same_size_class(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 a = 0, b = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 2);
    CHECK_EQ_INT((int)s_om_free(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 1);
    s_u32 c = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &c), S_OM_OK);
    CHECK_EQ_INT((int)c, (int)a); /* reused the freed slot */
    CHECK_EQ_INT((int)s_om_live_objects(h), 2);
    /* the reused slot is live again with correct identity/magic */
    CHECK_EQ_INT((int)s_om_validate(h, c), S_OM_OK);
    s_obj_kind k;
    CHECK_EQ_INT((int)s_om_kind(h, c, &k), S_OM_OK);
    CHECK_EQ_INT((int)k, S_OBJ_STR);
}

static void test_overflow_sizes(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0;
    /* A very large child count needs more room than the region; the request
     * is rejected deterministically (ENOMEM). */
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_ARRAY, S_OM_MAX_OBJECTS, 0, &o),
                 S_OM_ENOMEM);
    CHECK_EQ_INT((int)o, 0);
    /* A string payload of SIZE_MAX overflows the header+payload sum. */
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, (s_size)-1, &o),
                 S_OM_EOVERFLOW);
    CHECK_EQ_INT((int)o, 0);
}

static void test_oom_region_exhausted(void) {
    om_reset();
    s_om_heap h = 0;
    /* very small object region: holds exactly one 16-byte object */
    CHECK_EQ_INT((int)s_om_heap_create(64, 16, &h), S_OM_OK);
    s_u32 o1 = 0, o2 = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &o1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &o2), S_OM_ENOMEM);
    CHECK_EQ_INT((int)o2, 0);
    /* existing object intact */
    CHECK_EQ_INT((int)s_om_validate(h, o1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 1);
    /* after freeing, a new alloc succeeds via reuse */
    CHECK_EQ_INT((int)s_om_free(h, o1), S_OM_OK);
    s_u32 o3 = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &o3), S_OM_OK);
    CHECK_EQ_INT((int)o3, (int)o1);
}

static void test_fail_alloc(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0xDEADu;
    s_om_set_fail_alloc(true);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_ENOMEM);
    CHECK_EQ_INT((int)o, 0);
    s_om_set_fail_alloc(false);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
}

static void test_fail_after(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_om_set_alloc_fail_after(2);
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_ENOMEM);
    s_om_set_alloc_fail_after(0);
}

static void test_colour_set_get(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_OK);
    s_u8 col = 0;
    CHECK_EQ_INT((int)s_om_set_colour(h, o, S_GC_GREY), S_OM_OK);
    CHECK_EQ_INT((int)s_om_colour(h, o, &col), S_OM_OK);
    CHECK_EQ_INT((int)col, S_GC_GREY);
    CHECK_EQ_INT((int)s_om_set_colour(h, o, 99), S_OM_EINVAL); /* invalid colour */
}

static void test_large_object(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 65536, &h), S_OM_OK);
    s_size big = S_OM_SMALL_MAX + 64;
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, (s_u32)big, big, &o), S_OM_OK);
    s_size sz = 0;
    CHECK_EQ_INT((int)s_om_size(h, o, &sz), S_OM_OK);
    CHECK((s_size)sz >= S_OM_HEADER_SIZE + big);
    /* write/read across the large payload at both edges */
    s_u8 b = 0x5A;
    CHECK_EQ_INT((int)s_om_write_u8(h, o, 0, b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_read_u8(h, o, 0, &b), S_OM_OK);
    CHECK_EQ_INT((int)b, 0x5A);
    s_u8 c = 0x6B;
    CHECK_EQ_INT((int)s_om_write_u8(h, o, big - 1, c), S_OM_OK);
    CHECK_EQ_INT((int)s_om_read_u8(h, o, big - 1, &c), S_OM_OK);
    CHECK_EQ_INT((int)c, 0x6B);
    CHECK_EQ_INT((int)s_om_read_u8(h, o, big, &c), S_OM_EOOB);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
}

static void test_cleanup_and_leak(void) {
    om_reset();
    s_om_heap h1 = 0, h2 = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_heap_create(32, 2048, &h2), S_OM_OK);
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h2, S_OBJ_STR, 4, 4, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_heap_destroy(h1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_active_count(), 1);
    s_om_dispose_all();
    CHECK_EQ_INT((int)s_om_active_count(), 0);
    CHECK_EQ_INT((int)s_pm_active_count(), 0);
}

static void test_zero_len_str_and_array0(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_OK);
    s_u32 c = 0;
    CHECK_EQ_INT((int)s_om_count(h, o, &c), S_OM_OK);
    CHECK_EQ_INT((int)c, 0);
    /* zero-length access succeeds; non-zero fails (no payload) */
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_om_read(h, o, 0, &b, 0), S_OM_OK);
    CHECK_EQ_INT((int)s_om_read(h, o, 0, &b, 1), S_OM_EOOB);
    s_u32 a = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_ARRAY, 0, 0, &a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
}

TEST_MAIN_BEGIN()
    test_heap_lifecycle();
    test_heap_create_invalid();
    test_alloc_str_roundtrip();
    test_alloc_array_children();
    test_alloc_all_kinds();
    test_invalid_kind();
    test_payload_bounds();
    test_stale_and_double_free();
    test_invalid_heap_handle();
    test_reuse_same_size_class();
    test_overflow_sizes();
    test_oom_region_exhausted();
    test_fail_alloc();
    test_fail_after();
    test_colour_set_get();
    test_large_object();
    test_cleanup_and_leak();
    test_zero_len_str_and_array0();
TEST_MAIN_END()
