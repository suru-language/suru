#include "test_framework.h"
#include "suru/mem.h"

static void test_alloc_free(void) {
    s_heap *h = s_heap_create();
    CHECK(h != NULL);
    s_gcobj *s = s_heap_new_str(h, "hello", 5);
    CHECK(s != NULL);
    CHECK_EQ_INT(s->kind, S_OBJ_STR);
    CHECK_EQ_INT((int)s_obj_str_len(s), 5);
    CHECK(memcmp(s_obj_str_data(s), "hello", 5) == 0);

    s_gcobj *a = s_heap_new_array(h, 4);
    CHECK(a != NULL);
    s_obj_array_data(a)[0] = s_val_int(7);
    s_obj_array_data(a)[1] = s_val_obj(s);

    CHECK_EQ_INT((int)s_heap_live_objects(h), 2);
    s_heap_destroy(h);
}

static void test_collect_garbage(void) {
    s_heap *h = s_heap_create();
    /* allocate garbage that is not rooted */
    for (int i = 0; i < 100; i++) {
        s_gcobj *o = s_heap_new_str(h, "xyz", 3);
        (void)o;
    }
    CHECK_EQ_INT((int)s_heap_live_objects(h), 100);
    s_heap_set_stack_root(h, NULL, 0); /* no roots */
    s_heap_collect(h);
    CHECK_EQ_INT((int)s_heap_live_objects(h), 0);
    CHECK(s_heap_collections(h) > 0);
    s_heap_destroy(h);
}

static void test_cycle_collection(void) {
    s_heap *h = s_heap_create();
    /* build two records referencing each other */
    s_gcobj *a = s_heap_new_record(h, 1);
    s_gcobj *b = s_heap_new_record(h, 1);
    s_obj_record_fields(a)[0] = s_val_obj(b);
    s_obj_record_fields(b)[0] = s_val_obj(a);
    /* no roots -> both should be collected despite the cycle */
    s_heap_set_stack_root(h, NULL, 0);
    s_heap_collect(h);
    CHECK_EQ_INT((int)s_heap_live_objects(h), 0);
    s_heap_destroy(h);
}

static void test_stack_root(void) {
    s_heap *h = s_heap_create();
    s_value st[4];
    st[0] = s_val_int(1);
    s_gcobj *kept = s_heap_new_str(h, "keep", 4);
    st[1] = s_val_obj(kept);
    st[2] = s_val_none();
    s_heap_set_stack_root(h, st, 3);
    /* garbage */
    for (int i = 0; i < 50; i++) (void)s_heap_new_str(h, "g", 1);
    s_heap_collect(h);
    CHECK_EQ_INT((int)s_heap_live_objects(h), 1);
    CHECK_EQ_INT((int)s_heap_live_objects(h), 1);
    /* kept object still valid */
    CHECK(memcmp(s_obj_str_data(kept), "keep", 4) == 0);
    s_heap_destroy(h);
}

static void test_allocation_failure(void) {
    s_heap *h = s_heap_create();
    s_heap_set_fail_alloc(h, true);
    s_gcobj *o = s_heap_alloc(h, S_OBJ_STR, 1, 1);
    CHECK(o == NULL);
    s_heap_set_fail_alloc(h, false);
    o = s_heap_new_str(h, "ok", 2);
    CHECK(o != NULL);
    s_heap_destroy(h);
}

static void test_alloc_fail_after(void) {
    s_heap *h = s_heap_create();
    s_heap_set_alloc_fail_after(h, 3);
    int ok = 0;
    for (int i = 0; i < 10; i++) {
        if (s_heap_new_str(h, "x", 1)) ok++;
        else break;
    }
    CHECK_EQ_INT(ok, 3);
    s_heap_destroy(h);
}

static void test_incremental_step(void) {
    s_heap *h = s_heap_create();
    s_value st[1];
    s_gcobj *kept = s_heap_new_str(h, "keep", 4);
    st[0] = s_val_obj(kept);
    s_heap_set_stack_root(h, st, 1);
    /* garbage allocated between marking steps */
    for (int i = 0; i < 40; i++) (void)s_heap_new_str(h, "g", 1);
    /* run marking incrementally with a small budget, then sweep */
    int pending = 1;
    while ((pending = (int)s_heap_collect_step(h, 3)) > 0) { }
    CHECK_EQ_INT(pending, 0);
    s_heap_collect(h);
    CHECK_EQ_INT((int)s_heap_live_objects(h), 1);
    CHECK(memcmp(s_obj_str_data(kept), "keep", 4) == 0);
    s_heap_destroy(h);
}

static void test_incremental_graph(void) {
    s_heap *h = s_heap_create();
    s_gcobj *a = s_heap_new_record(h, 1);
    s_gcobj *b = s_heap_new_record(h, 1);
    s_obj_record_fields(a)[0] = s_val_obj(b);
    s_value st[1];
    st[0] = s_val_obj(a);
    s_heap_set_stack_root(h, st, 1);
    for (int i = 0; i < 25; i++) (void)s_heap_new_str(h, "junk", 4);
    /* incremental marking with tiny budgets must keep the whole graph */
    while (s_heap_collect_step(h, 2) > 0) { /* keep stepping */ }
    s_heap_collect(h);
    CHECK_EQ_INT((int)s_heap_live_objects(h), 2);
    CHECK(s_obj_record_fields(a)[0].u.o == b);
    s_heap_destroy(h);
}

static void test_exception_object_graph(void) {
    s_heap *h = s_heap_create();
    s_gcobj *type = s_heap_new_str(h, "FileError", 9);
    s_gcobj *message = s_heap_new_str(h, "read failed", 11);
    s_gcobj *cause_type = s_heap_new_str(h, "CauseError", 10);
    s_gcobj *cause_message = s_heap_new_str(h, "close failed", 12);
    s_gcobj *cause = s_heap_new_exception(h, s_val_obj(cause_type),
                                          s_val_obj(cause_message),
                                          s_val_none());
    s_gcobj *exception = s_heap_new_exception(h, s_val_obj(type),
                                              s_val_obj(message),
                                              s_val_obj(cause));
    CHECK(exception != NULL);
    CHECK_EQ_INT(exception->kind, S_OBJ_EXCEPTION);
    CHECK(s_obj_exception_fields(exception)[2].u.o == cause);
    s_value root[1] = { s_val_obj(exception) };
    s_heap_set_stack_root(h, root, 1);
    s_heap_collect(h);
    CHECK_EQ_INT((int)s_heap_live_objects(h), 6);
    CHECK(memcmp(s_obj_str_data(s_obj_exception_fields(exception)[0].u.o),
                 "FileError", 9) == 0);
    s_heap_destroy(h);
}

/* Differential tests against the SURU reference model (selfhost/memory/
 * ref_gc.suru). Each scenario uses the same graph shapes and survivor counts
 * as the model, so the C collector and the SURU model must agree.
 */
static void test_differential_ref_model(void) {
    /* chain-reachable: root -> n1 -> n2 => 3 survivors */
    {
        s_heap *h = s_heap_create();
        s_gcobj *n0 = s_heap_new_array(h, 1);
        s_gcobj *n1 = s_heap_new_array(h, 1);
        s_gcobj *n2 = s_heap_new_array(h, 0);
        (void)n2;
        s_obj_array_data(n0)[0] = s_val_obj(n1);
        s_obj_array_data(n1)[0] = s_val_obj(n2);
        s_value root[1] = { s_val_obj(n0) };
        s_heap_set_stack_root(h, root, 1);
        s_heap_collect(h);
        CHECK_EQ_INT((int)s_heap_live_objects(h), 3);
        s_heap_destroy(h);
    }
    /* unreachable-reclaimed: n2 orphaned => 2 survivors */
    {
        s_heap *h = s_heap_create();
        s_gcobj *n0 = s_heap_new_array(h, 1);
        s_gcobj *n1 = s_heap_new_array(h, 0);
        s_gcobj *n2 = s_heap_new_array(h, 0);
        (void)n2;
        s_obj_array_data(n0)[0] = s_val_obj(n1);
        s_value root[1] = { s_val_obj(n0) };
        s_heap_set_stack_root(h, root, 1);
        s_heap_collect(h);
        CHECK_EQ_INT((int)s_heap_live_objects(h), 2);
        s_heap_destroy(h);
    }
    /* cycle-root-survives: root -> a <-> b => 3 survivors */
    {
        s_heap *h = s_heap_create();
        s_gcobj *n0 = s_heap_new_array(h, 1);
        s_gcobj *a = s_heap_new_array(h, 1);
        s_gcobj *b = s_heap_new_array(h, 1);
        s_obj_array_data(n0)[0] = s_val_obj(a);
        s_obj_array_data(a)[0] = s_val_obj(b);
        s_obj_array_data(b)[0] = s_val_obj(a);
        s_value root[1] = { s_val_obj(n0) };
        s_heap_set_stack_root(h, root, 1);
        s_heap_collect(h);
        CHECK_EQ_INT((int)s_heap_live_objects(h), 3);
        s_heap_destroy(h);
    }
    /* multiple-roots: root0->n1, root2->n3 => 4 survivors */
    {
        s_heap *h = s_heap_create();
        s_gcobj *n0 = s_heap_new_array(h, 1);
        s_gcobj *n1 = s_heap_new_array(h, 0);
        s_gcobj *n2 = s_heap_new_array(h, 1);
        s_gcobj *n3 = s_heap_new_array(h, 0);
        s_obj_array_data(n0)[0] = s_val_obj(n1);
        s_obj_array_data(n2)[0] = s_val_obj(n3);
        s_value root[2] = { s_val_obj(n0), s_val_obj(n2) };
        s_heap_set_stack_root(h, root, 2);
        s_heap_collect(h);
        CHECK_EQ_INT((int)s_heap_live_objects(h), 4);
        s_heap_destroy(h);
    }
    /* root-removal-reclaims: remove root => 0 survivors */
    {
        s_heap *h = s_heap_create();
        s_gcobj *n0 = s_heap_new_array(h, 1);
        s_gcobj *n1 = s_heap_new_array(h, 0);
        s_obj_array_data(n0)[0] = s_val_obj(n1);
        s_value root[1] = { s_val_obj(n0) };
        s_heap_set_stack_root(h, root, 1);
        s_heap_collect(h);
        CHECK_EQ_INT((int)s_heap_live_objects(h), 2);
        s_heap_set_stack_root(h, NULL, 0);
        s_heap_collect(h);
        CHECK_EQ_INT((int)s_heap_live_objects(h), 0);
        s_heap_destroy(h);
    }
    /* nested-graph: root -> arr -> obj => 3 survivors */
    {
        s_heap *h = s_heap_create();
        s_gcobj *n0 = s_heap_new_array(h, 1);
        s_gcobj *n1 = s_heap_new_array(h, 1);
        s_gcobj *n2 = s_heap_new_array(h, 0);
        s_obj_array_data(n0)[0] = s_val_obj(n1);
        s_obj_array_data(n1)[0] = s_val_obj(n2);
        s_value root[1] = { s_val_obj(n0) };
        s_heap_set_stack_root(h, root, 1);
        s_heap_collect(h);
        CHECK_EQ_INT((int)s_heap_live_objects(h), 3);
        s_heap_destroy(h);
    }
}

TEST_MAIN_BEGIN()
    test_alloc_free();
    test_collect_garbage();
    test_cycle_collection();
    test_stack_root();
    test_allocation_failure();
    test_alloc_fail_after();
    test_incremental_step();
    test_incremental_graph();
    test_exception_object_graph();
    test_differential_ref_model();
TEST_MAIN_END()
