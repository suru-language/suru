#include "test_framework.h"
#include "suru/value.h"
#include "suru/objmodel.h"
#include "suru/platform_mem.h"

/* Stage 11 transition artifact: the VM's object references move from raw
 * s_gcobj* pointers to generation-protected handles carried in s_value.u.idx.
 * This test exercises the handle-based value builder and bounds-checked payload
 * access exactly as the runtime will use them. */
static void test_handle_value_roundtrip(void) {
    s_om_dispose_all();
    s_pm_dispose_all();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);

    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 5, 5, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_write(h, o, 0, "hello", 5), S_OM_OK);

    s_value v = s_val_obj_h((s_u64)o);
    CHECK_EQ_INT((int)v.tag, S_VAL_OBJ);
    CHECK_EQ_INT((int)s_val_obj_handle(v), (int)o);

    char buf[8] = {0};
    CHECK_EQ_INT((int)s_om_read(h, s_val_obj_handle(v), 0, buf, 5), S_OM_OK);
    CHECK_STR_EQ(buf, "hello");

    /* bounds still enforced through the handle value (payload capacity is the
     * rounded size minus header = 16 bytes) */
    CHECK_EQ_INT((int)s_om_read(h, s_val_obj_handle(v), 16, buf, 1), S_OM_EOOB);
}

static void test_handle_value_generation_identity(void) {
    s_om_dispose_all();
    s_pm_dispose_all();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &h), S_OM_OK);

    s_om_ref a = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 3, 3, &a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, a), S_OM_OK);
    s_om_ref b = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 3, 3, &b), S_OM_OK);

    /* the stale handle value is rejected; the live handle value is valid */
    s_value stale = s_val_obj_h((s_u64)a);
    s_value live = s_val_obj_h((s_u64)b);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(stale)), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(live)), S_OM_OK);
}

TEST_MAIN_BEGIN()
    test_handle_value_roundtrip();
    test_handle_value_generation_identity();
TEST_MAIN_END()
