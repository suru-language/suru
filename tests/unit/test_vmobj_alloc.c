#include "test_framework.h"
#include "suru/vmobj.h"
#include "suru/platform_mem.h"

#include <string.h>

static void om_reset(void) { s_om_dispose_all(); s_pm_dispose_all(); }

static void test_alloc_value_valid_handle(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_value v;
    memset(&v, 0, sizeof(v));
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_STR, 5, 5, &v), S_OM_OK);
    CHECK_EQ_INT((int)v.tag, S_VAL_OBJ);
    CHECK(s_val_obj_handle(v) != S_OM_REF_NONE);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(v)), S_OM_OK);
    /* full-width handle preserved: slot+generation decode */
    CHECK(s_val_obj_slot(v) >= 1);
    CHECK(s_val_obj_generation(v) >= 1);
    /* write/read through the value's handle */
    CHECK_EQ_INT((int)s_vmobj_write(h, s_val_obj_handle(v), 0, "hello", 5), S_OM_OK);
    char buf[8] = {0};
    CHECK_EQ_INT((int)s_vmobj_read(h, s_val_obj_handle(v), 0, buf, 5), S_OM_OK);
    CHECK_STR_EQ(buf, "hello");
}

static void test_alloc_value_stale_and_generation_mismatch(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_value a;
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_STR, 4, 4, &a), S_OM_OK);
    s_u32 a_slot = s_val_obj_slot(a);
    s_u32 a_gen = s_val_obj_generation(a);
    /* free -> stale */
    CHECK_EQ_INT((int)s_om_free(h, s_val_obj_handle(a)), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(a)), S_OM_ENOENT);
    /* reuse slot with new generation */
    s_value b;
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_STR, 4, 4, &b), S_OM_OK);
    CHECK_EQ_INT((int)s_val_obj_slot(b), (int)a_slot);
    CHECK(s_val_obj_generation(b) > a_gen);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(b)), S_OM_OK);
    /* stale a rejected, b valid */
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(a)), S_OM_ENOENT);
    /* generation mismatch rejected: a_gen+1 is the reused slot's current gen, so
     * use a clearly different (higher) generation */
    s_value bad = s_val_obj_h_parts(a_slot, a_gen + 2);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(bad)), S_OM_ENOENT);
    s_value bad2 = s_val_obj_h_parts(a_slot, a_gen); /* stale gen */
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(bad2)), S_OM_ENOENT);
}

static void test_alloc_value_generation_exhaustion(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_value a;
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_STR, 1, 1, &a), S_OM_OK);
    s_u32 slot = s_val_obj_slot(a);
    CHECK_EQ_INT((int)s_om_free(h, s_val_obj_handle(a)), S_OM_OK);
    CHECK_EQ_INT((int)s_om_test_set_generation(h, slot, S_OM_MAX_GEN), S_OM_OK);
    /* retired slot must not be reused; allocation proceeds from another slot */
    s_value b;
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_STR, 1, 1, &b), S_OM_OK);
    CHECK(s_val_obj_slot(b) != slot);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(b)), S_OM_OK);
}

static void test_alloc_value_failure_publishes_nothing(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_value v = s_val_none(); /* sentinel; must remain unchanged on failure */
    /* fault injection: OOM */
    s_om_set_fail_alloc(true);
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_STR, 1, 1, &v), S_OM_ENOMEM);
    CHECK_EQ_INT((int)v.tag, S_VAL_NONE);
    CHECK(!s_val_is_obj(v));
    s_om_set_fail_alloc(false);
    /* overflow: impossible payload */
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_STR, 0, (s_size)-1, &v),
                 S_OM_EOVERFLOW);
    CHECK_EQ_INT((int)v.tag, S_VAL_NONE);
    CHECK(!s_val_is_obj(v));
    /* null out */
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_STR, 0, 0, NULL), S_OM_EINVAL);
}

static void test_alloc_value_duplicate_destruction_rejected(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_value v;
    CHECK_EQ_INT((int)s_vmobj_alloc_value(h, S_OBJ_ARRAY, 0, 0, &v), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, s_val_obj_handle(v)), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, s_val_obj_handle(v)), S_OM_ENOENT);
}

static void test_alloc_value_nonobject_unaffected(void) {
    s_value iv = s_val_int(7);
    CHECK_EQ_INT((int)iv.tag, S_VAL_INT);
    CHECK(!s_val_is_obj(iv));
    s_value nv = s_val_none();
    CHECK_EQ_INT((int)nv.tag, S_VAL_NONE);
    CHECK(!s_val_is_obj(nv));
}

TEST_MAIN_BEGIN()
    test_alloc_value_valid_handle();
    test_alloc_value_stale_and_generation_mismatch();
    test_alloc_value_generation_exhaustion();
    test_alloc_value_failure_publishes_nothing();
    test_alloc_value_duplicate_destruction_rejected();
    test_alloc_value_nonobject_unaffected();
TEST_MAIN_END()
