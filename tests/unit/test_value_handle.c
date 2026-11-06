#include "test_framework.h"
#include "suru/value.h"
#include "suru/objmodel.h"
#include "suru/platform_mem.h"

/* Stage 11D-A: S_VAL_OBJ must carry a full 64-bit generation-protected handle
 * in u.idx, never a truncated 32-bit value and never a reinterpreted pointer. */

static s_value pass_through(s_value v) { return v; }

static void test_full_width_roundtrip(void) {
    s_u32 slot = 0x12345678u;
    s_u32 gen = 0x89ABCDEFu;
    s_u64 expect = ((s_u64)gen << 32) | (s_u64)slot;
    s_value v = s_val_obj_h_parts(slot, gen);
    CHECK_EQ_INT((int)v.tag, S_VAL_OBJ);
    CHECK_EQ_INT((long long)s_val_obj_handle(v), (long long)expect);
    CHECK_EQ_INT((long long)s_val_obj_id(v), (long long)expect);
    CHECK_EQ_INT((int)s_val_obj_slot(v), (int)slot);
    CHECK_EQ_INT((int)s_val_obj_generation(v), (int)gen);
    /* no truncation: high bits must be present */
    CHECK_EQ_INT((int)(s_val_obj_handle(v) >> 32), (int)gen);
}

static void test_copy_and_arg_return_preserve_handle(void) {
    s_u32 slot = 0x10000000u;
    s_u32 gen = 0xF0000000u;
    s_value a = s_val_obj_h_parts(slot, gen);
    s_value b = a;              /* struct copy */
    CHECK_EQ_INT((long long)s_val_obj_handle(a), (long long)s_val_obj_handle(b));
    s_value c = pass_through(a); /* argument/return transfer */
    CHECK_EQ_INT((long long)s_val_obj_handle(a), (long long)s_val_obj_handle(c));
    CHECK_EQ_INT((int)s_val_obj_slot(c), (int)slot);
    CHECK_EQ_INT((int)s_val_obj_generation(c), (int)gen);
}

static void test_stack_push_pop_preserve_handle(void) {
    s_value stk[8];
    s_size sp = 0;
    stk[sp++] = s_val_int(1);
    stk[sp++] = s_val_obj_h_parts(7, 0xA5A5A5A5u);
    stk[sp++] = s_val_none();
    sp--;
    s_value v = stk[--sp]; /* pop the handle */
    CHECK_EQ_INT((int)v.tag, S_VAL_OBJ);
    CHECK_EQ_INT((int)s_val_obj_slot(v), 7);
    CHECK_EQ_INT((int)s_val_obj_generation(v), (int)0xA5A5A5A5u);
    CHECK_EQ_INT((int)s_val_is_int(stk[0]), 1);
}

static void test_nonobject_values_unchanged(void) {
    s_i64 vi = 0x0123456789ABCDEFull;
    s_value iv = s_val_int(vi);
    CHECK_EQ_INT((int)iv.tag, S_VAL_INT);
    CHECK_EQ_INT((long long)iv.u.i, (long long)vi);
    CHECK(!s_val_is_obj(iv));

    s_value fv = s_val_float(2.5);
    CHECK_EQ_INT((int)fv.tag, S_VAL_FLOAT);
    CHECK(fv.u.f == 2.5);
    CHECK(!s_val_is_obj(fv));

    s_value bv = s_val_bool(true);
    CHECK_EQ_INT((int)bv.tag, S_VAL_BOOL);
    CHECK(bv.u.b);
    CHECK(!s_val_is_obj(bv));

    s_value nv = s_val_none();
    CHECK_EQ_INT((int)nv.tag, S_VAL_NONE);
    CHECK(!s_val_is_obj(nv));
    /* an INT whose bits equal a handle is never treated as an object */
    s_value trap = s_val_int((s_i64)0x89ABCDEF12345678ull);
    CHECK(!s_val_is_obj(trap));
}

static void test_zero_and_stale_rejected_at_resolution(void) {
    s_om_dispose_all();
    s_pm_dispose_all();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);

    /* zero handle */
    s_value zero = s_val_obj_h(0);
    CHECK_EQ_INT((int)zero.tag, S_VAL_OBJ);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(zero)), S_OM_ENOENT);

    /* live handle validates */
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 4, 4, &o), S_OM_OK);
    s_value live = s_val_obj_h((s_u64)o);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(live)), S_OM_OK);

    /* stale handle after free is rejected */
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, s_val_obj_handle(live)), S_OM_ENOENT);
}

static void test_handle_is_numeric_not_pointer(void) {
    s_value v = s_val_obj_h_parts(4, 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)s_val_obj_handle(v));
    CHECK_STR_EQ(buf, "8589934596"); /* (2<<32)|4 */
}

TEST_MAIN_BEGIN()
    test_full_width_roundtrip();
    test_copy_and_arg_return_preserve_handle();
    test_stack_push_pop_preserve_handle();
    test_nonobject_values_unchanged();
    test_zero_and_stale_rejected_at_resolution();
    test_handle_is_numeric_not_pointer();
TEST_MAIN_END()
