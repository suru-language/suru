#include "test_framework.h"
#include "suru/objmodel.h"
#include "suru/objgc.h"
#include "suru/platform_mem.h"

static void om_reset(void) { s_om_dispose_all(); s_pm_dispose_all(); }

static s_om_ref arr_with_kids(s_om_heap h, const s_om_ref *kids, s_u32 n) {
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_ARRAY, n, 0, &o), S_OM_OK);
    s_u32 i;
    for (i = 0; i < n; ++i) {
        CHECK_EQ_INT((int)s_om_write_u64(h, o, (s_size)i * 8, kids[i]),
                     S_OM_OK);
    }
    return o;
}

static void test_gc_lifecycle(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref ob = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &ob), S_OM_OK);
    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK(g != NULL);
    CHECK_EQ_INT((int)s_om_gc_nroots(g), 0);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, ob), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_nroots(g), 1);
    CHECK_EQ_INT((int)s_om_gc_remove_root(g, ob), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_nroots(g), 0);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, S_OM_REF_NONE), S_OM_EINVAL);
    s_om_gc_destroy(g);
    s_om_gc *bad = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(0, &bad), S_OM_ENOENT);
    CHECK(bad == NULL);
}

static void test_reachable_vs_unreachable(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref a = arr_with_kids(h, NULL, 0);
    s_om_ref b = S_OM_REF_NONE;
    { s_om_ref kids[1] = {a}; b = arr_with_kids(h, kids, 1); }
    s_om_ref c = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, b), S_OM_OK);

    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* only C reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, c), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_live_objects(h), 2);
    s_u8 col = 99;
    CHECK_EQ_INT((int)s_om_colour(h, b, &col), S_OM_OK);
    CHECK_EQ_INT((int)col, S_GC_WHITE);
    s_om_gc_destroy(g);
}

static void test_cycle_reachable(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref a = S_OM_REF_NONE, b = S_OM_REF_NONE;
    { s_om_ref k1[1] = {S_OM_REF_NONE}; a = arr_with_kids(h, k1, 1); }
    { s_om_ref k2[1] = {a}; b = arr_with_kids(h, k2, 1); }
    CHECK_EQ_INT((int)s_om_write_u64(h, a, 0, b), S_OM_OK);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, b), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_unreachable_cycle(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref d = S_OM_REF_NONE, e = S_OM_REF_NONE;
    { s_om_ref k1[1] = {S_OM_REF_NONE}; d = arr_with_kids(h, k1, 1); }
    { s_om_ref k2[1] = {d}; e = arr_with_kids(h, k2, 1); }
    CHECK_EQ_INT((int)s_om_write_u64(h, d, 0, e), S_OM_OK);
    s_om_ref g2 = arr_with_kids(h, NULL, 0);

    s_om_gc *gc = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &gc), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(gc, g2), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(gc), 2);
    CHECK_EQ_INT((int)s_om_validate(h, g2), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, d), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, e), S_OM_ENOENT);
    s_om_gc_destroy(gc);
}

static void test_alias_multiplicity(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref b = arr_with_kids(h, NULL, 0);
    s_om_ref a = S_OM_REF_NONE, c = S_OM_REF_NONE;
    { s_om_ref k[1] = {b}; a = arr_with_kids(h, k, 1); }
    { s_om_ref k[1] = {b}; c = arr_with_kids(h, k, 1); }

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, c), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, c), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 3);
    s_om_gc_destroy(g);
}

static void test_root_removal(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref a = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_remove_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

static void test_deep_chain(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(256, 65536, &h), S_OM_OK);
    enum { N = 40 };
    s_om_ref nodes[N];
    s_u32 i;
    for (i = 0; i < N; ++i) {
        s_om_ref kids[1] = {S_OM_REF_NONE};
        nodes[i] = arr_with_kids(h, kids, 1);
        if (i > 0) {
            CHECK_EQ_INT((int)s_om_write_u64(h, nodes[i], 0, nodes[i - 1]),
                         S_OM_OK);
        }
    }
    s_om_ref stray = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, nodes[N - 1]), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1);
    for (i = 0; i < N; ++i) {
        CHECK_EQ_INT((int)s_om_validate(h, nodes[i]), S_OM_OK);
    }
    CHECK_EQ_INT((int)s_om_validate(h, stray), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_live_objects(h), N);
    s_om_gc_destroy(g);
}

static void test_stale_root_ignored(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref a = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, s_om_test_make_handle(0xFFFFFFu, 1)),
                 S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_repeated_collect(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref a = arr_with_kids(h, NULL, 0);
    s_om_ref x = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, x), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 1);
    s_om_gc_destroy(g);
}

static void test_freelist_reuse_after_collect(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref root = arr_with_kids(h, NULL, 0);
    s_om_ref junk = arr_with_kids(h, NULL, 0);
    s_u32 junk_slot = s_om_test_slot_index(junk);
    s_u32 junk_gen = s_om_test_generation(junk);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, root), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1);

    s_om_ref revived = arr_with_kids(h, NULL, 0);
    CHECK_EQ_INT((int)s_om_test_slot_index(revived), (int)junk_slot);
    CHECK(s_om_test_generation(revived) > junk_gen);
    CHECK_EQ_INT((int)s_om_validate(h, root), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_collect_preserves_content(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref b = arr_with_kids(h, NULL, 0);
    s_om_ref a = S_OM_REF_NONE;
    { s_om_ref k[1] = {b}; a = arr_with_kids(h, k, 1); }
    s_om_ref junk = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);
    s_om_ref child = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_read_u64(h, a, 0, &child), S_OM_OK);
    CHECK_EQ_INT((int)child, (int)b);
    s_om_gc_destroy(g);
}

static void test_incremental_reachable_vs_unreachable(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref a = arr_with_kids(h, NULL, 0);
    s_om_ref b = S_OM_REF_NONE;
    { s_om_ref k[1] = {a}; b = arr_with_kids(h, k, 1); }
    s_om_ref c = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_OK);
    while (s_om_gc_mark_step(g, 1)) {
    }
    CHECK_EQ_INT((int)s_om_gc_sweep(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, c), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_live_objects(h), 2);
    s_om_gc_destroy(g);
}

static void test_incremental_eventual_completion(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref root = arr_with_kids(h, NULL, 0);
    s_om_ref junk = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, root), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_OK);
    int steps = 0;
    while (s_om_gc_mark_step(g, 1)) {
        steps++;
        if (steps > 100000) break;
    }
    CHECK(steps < 100000);
    CHECK_EQ_INT((int)s_om_gc_sweep(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, root), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

static void test_incremental_under_pressure(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(256, 65536, &h), S_OM_OK);
    s_om_ref root = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, root), S_OM_OK);

    int cycle;
    for (cycle = 0; cycle < 30; ++cycle) {
        s_om_ref junk = arr_with_kids(h, NULL, 0);
        CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_OK);
        while (s_om_gc_mark_step(g, 3)) {
        }
        CHECK_EQ_INT((int)s_om_gc_sweep(g), 1);
        CHECK_EQ_INT((int)s_om_validate(h, root), S_OM_OK);
        CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);
        CHECK_EQ_INT((int)s_om_live_objects(h), 1);
    }
    s_om_gc_destroy(g);
}

static void test_allocation_pressure(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(256, 65536, &h), S_OM_OK);
    s_om_ref root = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, root), S_OM_OK);

    int cycle;
    for (cycle = 0; cycle < 50; ++cycle) {
        s_om_ref c = arr_with_kids(h, NULL, 0);
        s_om_ref d = arr_with_kids(h, NULL, 0);
        s_om_ref e = arr_with_kids(h, NULL, 0);
        CHECK_EQ_INT((int)s_om_gc_collect(g), 3);
        CHECK_EQ_INT((int)s_om_validate(h, root), S_OM_OK);
        CHECK_EQ_INT((int)s_om_validate(h, c), S_OM_ENOENT);
        CHECK_EQ_INT((int)s_om_validate(h, d), S_OM_ENOENT);
        CHECK_EQ_INT((int)s_om_validate(h, e), S_OM_ENOENT);
        CHECK_EQ_INT((int)s_om_live_objects(h), 1);
    }
    s_om_gc_destroy(g);
}

static void test_stw_blocks_mutation_during_marking(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref live = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, live), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_OK);
    CHECK(s_om_gc_marking(g));
    CHECK(s_om_is_collecting(h));

    s_om_ref o = s_om_test_make_handle(0xDEADu, 1);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_ECOLLECT);
    CHECK_EQ_INT((int)o, 0);
    CHECK_EQ_INT((int)s_om_free(h, live), S_OM_ECOLLECT);
    CHECK_EQ_INT((int)s_om_write_u8(h, live, 0, 0x55), S_OM_ECOLLECT);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, live), S_OM_ECOLLECT);
    CHECK_EQ_INT((int)s_om_gc_remove_root(g, live), S_OM_ECOLLECT);
    CHECK_EQ_INT((int)s_om_validate(h, live), S_OM_OK);

    while (s_om_gc_mark_step(g, 1)) {
    }
    CHECK(!s_om_gc_marking(g));
    CHECK(!s_om_is_collecting(h));
    CHECK_EQ_INT((int)s_om_gc_sweep(g), 0);
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_STR, 1, 1, &o), S_OM_OK);
    CHECK_EQ_INT((int)s_om_free(h, o), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_begin_twice_and_collect_while_active_refused(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref root = arr_with_kids(h, NULL, 0);
    s_om_ref junk = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, root), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_ECOLLECT);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0);
    while (s_om_gc_mark_step(g, 1)) {
    }
    CHECK_EQ_INT((int)s_om_gc_sweep(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, root), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

static void test_sweep_before_completion_noop(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref root = arr_with_kids(h, NULL, 0);
    s_om_ref junk = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, root), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_sweep(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_OK);
    while (s_om_gc_mark_step(g, 1)) {
    }
    CHECK_EQ_INT((int)s_om_gc_sweep(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, root), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_gc_stale_child_reference(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);

    /* Parent P validly references child C. */
    s_om_ref c = arr_with_kids(h, NULL, 0);
    s_u32 c_slot = s_om_test_slot_index(c);
    s_om_ref p = S_OM_REF_NONE;
    { s_om_ref k[1] = {c}; p = arr_with_kids(h, k, 1); }

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, p), S_OM_OK);
    /* valid child is marked and survives */
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, c), S_OM_OK);

    /* Release C; it is no longer rooted (P's handle is now stale). */
    CHECK_EQ_INT((int)s_om_free(h, c), S_OM_OK);
    /* Reuse C's slot for D with a new generation. */
    s_om_ref d = arr_with_kids(h, NULL, 0);
    CHECK_EQ_INT((int)s_om_test_slot_index(d), (int)c_slot);
    CHECK(s_om_test_generation(d) > s_om_test_generation(c));

    /* Collect: P's stale child handle must be ignored, so D is unreachable and
     * must be reclaimed; the stale handle must NOT mark or preserve D. */
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, p), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, d), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

static void test_gc_generation_marks_correct_object(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);

    /* A root references a live child via a VALID handle. */
    s_om_ref child = arr_with_kids(h, NULL, 0);
    s_om_ref root = S_OM_REF_NONE;
    { s_om_ref k[1] = {child}; root = arr_with_kids(h, k, 1); }

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, root), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, child), S_OM_OK);

    /* Now free a DIFFERENT object occupying a different slot; the valid child
     * handle must still identify the correct generation after other activity. */
    s_om_ref extra = arr_with_kids(h, NULL, 0);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* extra reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, extra), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, child), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, root), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_gc_preserves_corrupt_reachable(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);

    /* Parent validly references child; then we corrupt the CHILD's inline
     * header. The object is still identified (slot+generation intact), so the
     * collector must preserve it; the corruption is surfaced by validation. */
    s_om_ref child = arr_with_kids(h, NULL, 0);
    s_om_ref parent = S_OM_REF_NONE;
    { s_om_ref k[1] = {child}; parent = arr_with_kids(h, k, 1); }
    CHECK_EQ_INT((int)s_om_test_set_hdr_u32(h, child, 8, 0xDEADBEEFu), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, child), S_OM_EINVAL);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, parent), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0); /* nothing reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, parent), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 2);
    /* child still present (identity live), flagged corrupt by strict validation */
    s_u32 cur = 0;
    s_om_ref o = S_OM_REF_NONE;
    int n = 0;
    int saw_child = 0;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) {
        n++;
        if (o == child) saw_child = 1;
    }
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_INT(saw_child, 1);
    s_om_gc_destroy(g);
}

static void test_stack_range_root_preserves(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref live = arr_with_kids(h, NULL, 0);
    s_om_ref junk = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    s_om_ref stk[4];
    stk[0] = live;
    CHECK_EQ_INT((int)s_om_gc_set_stack_root(g, stk, 1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* only junk reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, live), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

static void test_stack_range_stale_ignored(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref live = arr_with_kids(h, NULL, 0);
    s_om_ref ghost = arr_with_kids(h, NULL, 0);
    s_u32 ghost_slot = s_om_test_slot_index(ghost);
    CHECK_EQ_INT((int)s_om_free(h, ghost), S_OM_OK); /* stale handle */
    s_om_ref ghost_stale = s_om_test_make_handle(ghost_slot, 1);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    s_om_ref stk[4];
    stk[0] = live;
    stk[1] = ghost_stale;                 /* stale (wrong gen) */
    stk[2] = s_om_test_make_handle(0xFFFFFFu, 1); /* out-of-range */
    CHECK_EQ_INT((int)s_om_gc_set_stack_root(g, stk, 3), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0); /* nothing extra reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, live), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_stack_range_clear(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref obj = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    s_om_ref stk[4];
    stk[0] = obj;
    CHECK_EQ_INT((int)s_om_gc_set_stack_root(g, stk, 1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0); /* preserved via stack root */
    CHECK_EQ_INT((int)s_om_validate(h, obj), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_set_stack_root(g, NULL, 0), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* now reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, obj), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

static void test_stack_root_during_marking_refused(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref live = arr_with_kids(h, NULL, 0);
    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_mark_begin(g), S_OM_OK);
    s_om_ref stk[4];
    stk[0] = live;
    CHECK_EQ_INT((int)s_om_gc_set_stack_root(g, stk, 1), S_OM_ECOLLECT);
    while (s_om_gc_mark_step(g, 1)) {
    }
    CHECK_EQ_INT((int)s_om_gc_sweep(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, live), S_OM_OK);
    s_om_gc_destroy(g);
}

TEST_MAIN_BEGIN()
    test_gc_lifecycle();
    test_reachable_vs_unreachable();
    test_cycle_reachable();
    test_unreachable_cycle();
    test_alias_multiplicity();
    test_root_removal();
    test_deep_chain();
    test_stale_root_ignored();
    test_repeated_collect();
    test_freelist_reuse_after_collect();
    test_collect_preserves_content();
    test_incremental_reachable_vs_unreachable();
    test_incremental_eventual_completion();
    test_incremental_under_pressure();
    test_allocation_pressure();
    test_stw_blocks_mutation_during_marking();
    test_begin_twice_and_collect_while_active_refused();
    test_sweep_before_completion_noop();
    test_gc_stale_child_reference();
    test_gc_generation_marks_correct_object();
    test_gc_preserves_corrupt_reachable();
    test_stack_range_root_preserves();
    test_stack_range_stale_ignored();
    test_stack_range_clear();
    test_stack_root_during_marking_refused();
TEST_MAIN_END()
