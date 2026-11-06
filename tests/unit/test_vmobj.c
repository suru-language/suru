#include "test_framework.h"
#include "suru/vmobj.h"
#include "suru/platform_mem.h"

#include <string.h>

static void om_reset(void) { s_om_dispose_all(); s_pm_dispose_all(); }

static void test_alloc_validate_is_live(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_vmobj_alloc(h, S_OBJ_STR, 5, 5, &o), S_OM_OK);
    CHECK(o != S_OM_REF_NONE);
    CHECK_EQ_INT((int)s_vmobj_validate(h, o), S_OM_OK);
    CHECK(s_vmobj_is_live(h, o));
    /* invalid / stale / zero */
    CHECK_EQ_INT((int)s_vmobj_validate(h, S_OM_REF_NONE), S_OM_ENOENT);
    CHECK(!s_vmobj_is_live(h, S_OM_REF_NONE));
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_validate(h, o), S_OM_ENOENT);
    CHECK(!s_vmobj_is_live(h, o));
    /* allocation failure publishes no handle */
    s_om_ref o2 = s_om_test_make_handle(0xDEADu, 1);
    s_om_set_fail_alloc(true);
    CHECK_EQ_INT((int)s_vmobj_alloc(h, S_OBJ_STR, 1, 1, &o2), S_OM_ENOMEM);
    CHECK_EQ_INT((int)o2, 0);
    s_om_set_fail_alloc(false);
}

static void test_read_write_payload(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_vmobj_alloc(h, S_OBJ_STR, 8, 8, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_write(h, o, 0, "abcdefgh", 8), S_OM_OK);
    char buf[16] = {0};
    CHECK_EQ_INT((int)s_vmobj_read(h, o, 0, buf, 8), S_OM_OK);
    CHECK_STR_EQ(buf, "abcdefgh");
    CHECK_EQ_INT((int)s_vmobj_write_u32(h, o, 8, 0x11223344u), S_OM_OK);
    s_u32 u = 0;
    CHECK_EQ_INT((int)s_vmobj_read_u32(h, o, 8, &u), S_OM_OK);
    CHECK_EQ_INT((int)u, (int)0x11223344u);
    CHECK_EQ_INT((int)s_vmobj_read_u8(h, o, 0, (s_u8 *)buf), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_read(h, o, 16, buf, 1), S_OM_EOOB);
    CHECK_EQ_INT((int)s_vmobj_write(h, o, 16, buf, 1), S_OM_EOOB);
}

static void test_inspect_view(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_vmobj_alloc(h, S_OBJ_STR, 6, 6, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_write(h, o, 0, "foobar", 6), S_OM_OK);
    const void *base = NULL;
    s_size rounded = 0;
    CHECK_EQ_INT((int)s_vmobj_inspect(h, o, &base, &rounded), S_OM_OK);
    CHECK(base != NULL);
    CHECK(rounded >= S_OM_HEADER_SIZE + 6);
    const unsigned char *p = (const unsigned char *)base;
    CHECK_EQ_INT((int)memcmp(p + S_OM_HEADER_SIZE, "foobar", 6), 0);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_inspect(h, o, &base, &rounded), S_OM_ENOENT);
}

static void test_temp_root_survival(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    s_om_ref live = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_vmobj_alloc(h, S_OBJ_ARRAY, 0, 0, &live), S_OM_OK);
    s_om_ref junk = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_vmobj_alloc(h, S_OBJ_ARRAY, 0, 0, &junk), S_OM_OK);
    /* only `live` is a temporary root */
    CHECK_EQ_INT((int)s_vmobj_temp_root(g, live), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_collect(g), 1); /* junk reclaimed */
    CHECK_EQ_INT((int)s_vmobj_validate(h, live), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_validate(h, junk), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_vmobj_temp_unroot(g, live), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_collect(g), 1); /* live now reclaimed */
    CHECK_EQ_INT((int)s_vmobj_validate(h, live), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

static void test_temp_root_refused_during_marking(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref live = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_vmobj_alloc(h, S_OBJ_ARRAY, 0, 0, &live), S_OM_OK);
    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_OK);
    CHECK_EQ_INT((int)s_vmobj_temp_root(g, live), S_OM_ECOLLECT);
    while (s_om_gc_mark_step(g, 1)) {
    }
    CHECK_EQ_INT((int)s_om_gc_sweep(g), 1); /* live unrooted -> reclaimed */
    CHECK_EQ_INT((int)s_vmobj_validate(h, live), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

TEST_MAIN_BEGIN()
    test_alloc_validate_is_live();
    test_read_write_payload();
    test_inspect_view();
    test_temp_root_survival();
    test_temp_root_refused_during_marking();
TEST_MAIN_END()
