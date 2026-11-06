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
    s_om_ref o = S_OM_REF_NONE;
    const char *msg = "hello suru";
    s_size len = (s_size)strlen(msg);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, (s_u32)len, len, &o), S_OM_OK);
    CHECK(o != S_OM_REF_NONE);
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
    s_om_ref o = S_OM_REF_NONE;
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
        s_om_ref o = S_OM_REF_NONE;
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
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, (s_obj_kind)99, 0, 0, &o), S_OM_EINVAL);
    CHECK_EQ_INT((int)o, 0);
}

static void test_payload_bounds(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_om_ref o = S_OM_REF_NONE;
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
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 4, 4, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_ENOENT);
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_om_read(h, o, 0, &b, 1), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, S_OM_REF_NONE), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, s_om_test_make_handle(0xFFFFFFu, 1)),
                 S_OM_ENOENT);
}

static void test_invalid_heap_handle(void) {
    om_reset();
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(0, S_OBJ_STR, 1, 1, &o), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(0xDEADu, s_om_test_make_handle(1, 1)),
                 S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_heap_destroy(0), S_OM_ENOENT);
}

static void test_reuse_same_size_class(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_om_ref a = S_OM_REF_NONE, b = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 2);
    CHECK_EQ_INT((int)s_om_free(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 1);
    s_om_ref c = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &c), S_OM_OK);
    /* same slot reused, but a different (advanced) generation */
    CHECK_EQ_INT((int)s_om_test_slot_index(c), (int)s_om_test_slot_index(a));
    CHECK(s_om_test_generation(c) > s_om_test_generation(a));
    CHECK_EQ_INT((int)s_om_live_objects(h), 2);
    CHECK_EQ_INT((int)s_om_validate(h, c), S_OM_OK);
    s_obj_kind k;
    CHECK_EQ_INT((int)s_om_kind(h, c, &k), S_OM_OK);
    CHECK_EQ_INT((int)k, S_OBJ_STR);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_ENOENT);
}

static void test_overflow_sizes(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_ARRAY, S_OM_MAX_OBJECTS, 0, &o),
                 S_OM_ENOMEM);
    CHECK_EQ_INT((int)o, 0);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, (s_size)-1, &o),
                 S_OM_EOVERFLOW);
    CHECK_EQ_INT((int)o, 0);
}

static void test_oom_region_exhausted(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 16, &h), S_OM_OK);
    s_om_ref o1 = S_OM_REF_NONE, o2 = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &o1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &o2), S_OM_ENOMEM);
    CHECK_EQ_INT((int)o2, 0);
    CHECK_EQ_INT((int)s_om_validate(h, o1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 1);
    CHECK_EQ_INT((int)s_om_free(h, o1), S_OM_OK);
    s_om_ref o3 = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &o3), S_OM_OK);
    CHECK_EQ_INT((int)s_om_test_slot_index(o3), (int)s_om_test_slot_index(o1));
}

static void test_fail_alloc(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_om_ref o = s_om_test_make_handle(0xDEADu, 1);
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
    s_om_ref o = S_OM_REF_NONE;
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
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_OK);
    s_u8 col = 0;
    CHECK_EQ_INT((int)s_om_set_colour(h, o, S_GC_GREY), S_OM_OK);
    CHECK_EQ_INT((int)s_om_colour(h, o, &col), S_OM_OK);
    CHECK_EQ_INT((int)col, S_GC_GREY);
    CHECK_EQ_INT((int)s_om_set_colour(h, o, 99), S_OM_EINVAL);
}

static void test_large_object(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 65536, &h), S_OM_OK);
    s_size big = S_OM_SMALL_MAX + 64;
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, (s_u32)big, big, &o), S_OM_OK);
    s_size sz = 0;
    CHECK_EQ_INT((int)s_om_size(h, o, &sz), S_OM_OK);
    CHECK((s_size)sz >= S_OM_HEADER_SIZE + big);
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
    s_om_ref o = S_OM_REF_NONE;
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
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_OK);
    s_u32 c = 0;
    CHECK_EQ_INT((int)s_om_count(h, o, &c), S_OM_OK);
    CHECK_EQ_INT((int)c, 0);
    unsigned char b = 0;
    CHECK_EQ_INT((int)s_om_read(h, o, 0, &b, 0), S_OM_OK);
    CHECK_EQ_INT((int)s_om_read(h, o, 0, &b, 1), S_OM_EOOB);
    s_om_ref a = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_ARRAY, 0, 0, &a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
}

static void test_next_live(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);
    s_om_ref o1 = S_OM_REF_NONE, o2 = S_OM_REF_NONE, o3 = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o2), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o3), S_OM_OK);
    s_u32 cur = 0;
    s_om_ref o = S_OM_REF_NONE;
    int count = 0;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) count++;
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT((int)s_om_free(h, o2), S_OM_OK);
    cur = 0;
    count = 0;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) count++;
    CHECK_EQ_INT(count, 2);
    s_om_ref bad = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_next_live(h, NULL, &bad), S_OM_EINVAL);
    CHECK_EQ_INT((int)s_om_next_live(h, &cur, NULL), S_OM_EINVAL);
}

static void test_small_large_boundary(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_size p = S_OM_SMALL_MAX - S_OM_HEADER_SIZE;
    s_om_ref o1 = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, (s_u32)p, p, &o1), S_OM_OK);
    s_size sz1 = 0;
    CHECK_EQ_INT((int)s_om_size(h, o1, &sz1), S_OM_OK);
    CHECK_EQ_INT((int)sz1, S_OM_SMALL_MAX);
    s_size q = S_OM_SMALL_MAX + 16;
    s_om_ref o2 = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, (s_u32)q, q, &o2), S_OM_OK);
    s_size sz2 = 0;
    CHECK_EQ_INT((int)s_om_size(h, o2, &sz2), S_OM_OK);
    CHECK((s_size)sz2 >= S_OM_HEADER_SIZE + q);
    CHECK_EQ_INT((int)s_om_validate(h, o1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, o2), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o2), S_OM_OK);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, (s_u32)p, p, &o1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_test_slot_index(o1), 1);
}

/* ------------------------------------------------------------------ */
/* Phase 3F.1: generation-protected handle regression tests             */
/* ------------------------------------------------------------------ */

static void test_generation_stale_handle_vulnerability(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    /* Allocate A, save its handle. */
    s_om_ref a = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 4, 4, &a), S_OM_OK);
    s_u32 a_slot = s_om_test_slot_index(a);
    s_u32 a_gen = s_om_test_generation(a);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);

    /* Release A -> old handle invalidated immediately. */
    CHECK_EQ_INT((int)s_om_free(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_ENOENT);

    /* Reuse the same slot for B. */
    s_om_ref b = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 4, 4, &b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_test_slot_index(b), (int)a_slot);
    CHECK(s_om_test_generation(b) != a_gen);
    CHECK_EQ_INT((int)s_om_validate(h, b), S_OM_OK);

    /* Write B; stale A must not be able to read, modify, or free B. */
    CHECK_EQ_INT((int)s_om_write_u8(h, b, 0, 0xAA), S_OM_OK);
    s_u8 x = 0;
    CHECK_EQ_INT((int)s_om_read_u8(h, a, 0, &x), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_write_u8(h, a, 0, 0x55), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_free(h, a), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_read_u8(h, b, 0, &x), S_OM_OK);
    CHECK_EQ_INT((int)x, 0xAA);
    CHECK_EQ_INT((int)s_om_validate(h, b), S_OM_OK);
}

static void test_handle_basics(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref a = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &a), S_OM_OK);
    /* zero handle rejected */
    CHECK_EQ_INT((int)s_om_validate(h, S_OM_REF_NONE), S_OM_ENOENT);
    /* invalid slot rejected */
    CHECK_EQ_INT((int)s_om_validate(h, s_om_test_make_handle(9999, 1)),
                 S_OM_ENOENT);
    /* invalid generation rejected */
    CHECK_EQ_INT((int)s_om_validate(h,
                 s_om_test_make_handle(s_om_test_slot_index(a),
                                       s_om_test_generation(a) + 1)),
                 S_OM_ENOENT);
    /* duplicate references to the same live object remain valid */
    s_om_ref d = a;
    CHECK_EQ_INT((int)s_om_validate(h, d), S_OM_OK);
    /* handle comparison distinguishes generations */
    CHECK(s_om_test_make_handle(s_om_test_slot_index(a), s_om_test_generation(a))
          == a);
    CHECK(s_om_test_make_handle(s_om_test_slot_index(a), s_om_test_generation(a) + 1)
          != a);
}

static void test_generation_advance_across_reuse(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 65536, &h), S_OM_OK);
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 4, 4, &o), S_OM_OK);
    s_u32 slot = s_om_test_slot_index(o);
    s_u32 prev_gen = s_om_test_generation(o);
    CHECK_EQ_INT((int)prev_gen, 1);
    int i;
    for (i = 0; i < 20; ++i) {
        CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
        CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 4, 4, &o), S_OM_OK);
        CHECK_EQ_INT((int)s_om_test_slot_index(o), (int)slot);
        CHECK(s_om_test_generation(o) > prev_gen);
        prev_gen = s_om_test_generation(o);
        CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_OK);
    }
    CHECK_EQ_INT((int)prev_gen, 21);
}

static void test_generation_exhaustion_retires_slot(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref a = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &a), S_OM_OK);
    s_u32 a_slot = s_om_test_slot_index(a);
    CHECK_EQ_INT((int)s_om_free(h, a), S_OM_OK);
    /* force the freed slot to its maximum generation */
    CHECK_EQ_INT((int)s_om_test_set_generation(h, a_slot, S_OM_MAX_GEN), S_OM_OK);
    /* next allocation must NOT reuse the retired slot */
    s_om_ref b = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &b), S_OM_OK);
    CHECK(s_om_test_slot_index(b) != a_slot);
    CHECK_EQ_INT((int)s_om_validate(h, b), S_OM_OK);
    /* retiring one slot does not exhaust the heap */
    CHECK_EQ_INT((int)s_om_live_objects(h), 1);
}

static void test_generation_exhaustion_all_slots_retired(void) {
    om_reset();
    s_om_heap h = 0;
    /* Single-slot heap: retiring the only slot exhausts allocation. */
    CHECK_EQ_INT((int)s_om_heap_create(1, 64, &h), S_OM_OK);
    s_om_ref a = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_test_slot_index(a), 1);
    CHECK_EQ_INT((int)s_om_free(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_test_set_generation(h, 1, S_OM_MAX_GEN), S_OM_OK);
    s_om_ref b = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 0, 0, &b), S_OM_ENOMEM);
    CHECK_EQ_INT((int)b, 0); /* no partial/invalid handle published */
}

static s_om_ref fresh_str(s_om_heap h) {
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &o), S_OM_OK);
    return o;
}

static void test_metadata_corruption_detected(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);

    /* invalid magic */
    { s_om_ref o = fresh_str(h);
      CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_OK);
      CHECK_EQ_INT((int)s_om_test_set_hdr_u32(h, o, 8, 0xDEADBEEFu), S_OM_OK);
      CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_EINVAL); }
    /* unknown kind */
    { s_om_ref o = fresh_str(h);
      CHECK_EQ_INT((int)s_om_test_set_hdr_u8(h, o, 12, 99), S_OM_OK);
      CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_EINVAL); }
    /* invalid colour */
    { s_om_ref o = fresh_str(h);
      CHECK_EQ_INT((int)s_om_test_set_hdr_u8(h, o, 13, 9), S_OM_OK);
      CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_EINVAL); }
    /* invalid flags */
    { s_om_ref o = fresh_str(h);
      CHECK_EQ_INT((int)s_om_test_set_hdr_u8(h, o, 14, 1), S_OM_OK);
      CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_EINVAL); }
    /* impossible size: zero */
    { s_om_ref o = fresh_str(h);
      CHECK_EQ_INT((int)s_om_test_set_hdr_u32(h, o, 0, 0), S_OM_OK);
      CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_EINVAL); }
    /* impossible size: non-aligned */
    { s_om_ref o = fresh_str(h);
      CHECK_EQ_INT((int)s_om_test_set_hdr_u32(h, o, 0, 0x123u), S_OM_OK);
      CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_EINVAL); }
    /* impossible size: out of region */
    { s_om_ref o = fresh_str(h);
      CHECK_EQ_INT((int)s_om_test_set_hdr_u32(h, o, 0, 0x7FFFFFF0u), S_OM_OK);
      CHECK_EQ_INT((int)s_om_validate(h, o), S_OM_EINVAL); }
    /* out-of-range header offset rejected by the hook */
    { s_om_ref o = fresh_str(h);
      CHECK_EQ_INT((int)s_om_test_set_hdr_u8(h, o, 16, 1), S_OM_EINVAL); }
}

static void test_inspect_stable_view(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 8, 8, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_write(h, o, 0, "abcdefgh", 8), S_OM_OK);

    const void *base = NULL;
    s_size rounded = 0;
    CHECK_EQ_INT((int)s_om_inspect(h, o, &base, &rounded), S_OM_OK);
    CHECK(base != NULL);
    CHECK(rounded >= S_OM_HEADER_SIZE + 8);
    CHECK_EQ_INT((int)(rounded % S_OM_ALIGN), 0);

    const unsigned char *p = (const unsigned char *)base;
    s_u32 magic = 0;
    memcpy(&magic, p + 8, 4);
    CHECK_EQ_INT((int)magic, (int)S_OBJ_MAGIC);
    CHECK_EQ_INT((int)memcmp(p + S_OM_HEADER_SIZE, "abcdefgh", 8), 0);

    /* stale handle is rejected */
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_inspect(h, o, &base, &rounded), S_OM_ENOENT);
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
    test_next_live();
    test_small_large_boundary();
    test_generation_stale_handle_vulnerability();
    test_handle_basics();
    test_generation_advance_across_reuse();
    test_generation_exhaustion_retires_slot();
    test_generation_exhaustion_all_slots_retired();
    test_metadata_corruption_detected();
    test_inspect_stable_view();
TEST_MAIN_END()
