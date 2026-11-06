#include "test_framework.h"
#include "suru/objmodel.h"
#include "suru/objgc.h"
#include "suru/platform_mem.h"

static void om_reset(void) { s_om_dispose_all(); s_pm_dispose_all(); }

static s_u32 arr_with_kids(s_om_heap h, const s_u32 *kids, s_u32 n) {
    s_u32 o = 0;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_ARRAY, n, 0, &o), S_OM_OK);
    s_u32 i;
    for (i = 0; i < n; ++i) {
        CHECK_EQ_INT((int)s_om_write_u32(h, o, (s_size)i * 4, kids[i]),
                     S_OM_OK);
    }
    return o;
}

static void test_gc_lifecycle(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK(g != NULL);
    CHECK_EQ_INT((int)s_om_gc_nroots(g), 0);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, 1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_nroots(g), 1);
    CHECK_EQ_INT((int)s_om_gc_remove_root(g, 1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_nroots(g), 0);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, 0), S_OM_EINVAL);
    s_om_gc_destroy(g);
    s_om_gc *bad = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(0, &bad), S_OM_ENOENT);
    CHECK(bad == NULL);
}

static void test_reachable_vs_unreachable(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    /* A -> B ; C is isolated garbage */
    s_u32 a = arr_with_kids(h, NULL, 0);
    s_u32 b = 0;
    { s_u32 kids[1] = {a}; b = arr_with_kids(h, kids, 1); }
    s_u32 c = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, b), S_OM_OK);

    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* only C reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, b), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, c), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_live_objects(h), 2);
    /* surviving objects are reset to white */
    s_u8 col = 99;
    CHECK_EQ_INT((int)s_om_colour(h, b, &col), S_OM_OK);
    CHECK_EQ_INT((int)col, S_GC_WHITE);
    s_om_gc_destroy(g);
}

static void test_cycle_reachable(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    /* A <-> B, rooted at A */
    s_u32 a = 0, b = 0;
    { s_u32 k1[1] = {0}; a = arr_with_kids(h, k1, 1); }
    { s_u32 k2[1] = {a}; b = arr_with_kids(h, k2, 1); }
    { s_u32 k3[1] = {b}; CHECK_EQ_INT((int)s_om_write_u32(h, a, 0, k3[0]), S_OM_OK); }

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
    /* D <-> E is garbage; keep a plain root G live */
    s_u32 d = 0, e = 0;
    { s_u32 k1[1] = {0}; d = arr_with_kids(h, k1, 1); }
    { s_u32 k2[1] = {d}; e = arr_with_kids(h, k2, 1); }
    { s_u32 k3[1] = {e}; CHECK_EQ_INT((int)s_om_write_u32(h, d, 0, k3[0]), S_OM_OK); }
    s_u32 g = arr_with_kids(h, NULL, 0);

    s_om_gc *gc = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &gc), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(gc, g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(gc), 2); /* D and E reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, d), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, e), S_OM_ENOENT);
    s_om_gc_destroy(gc);
}

static void test_alias_multiplicity(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    /* A and C both reference B; B must stay live exactly once */
    s_u32 b = arr_with_kids(h, NULL, 0);
    s_u32 a = 0, c = 0;
    { s_u32 k[1] = {b}; a = arr_with_kids(h, k, 1); }
    { s_u32 k[1] = {b}; c = arr_with_kids(h, k, 1); }

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
    s_u32 a = arr_with_kids(h, NULL, 0);

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
    s_u32 nodes[N];
    s_u32 i;
    for (i = 0; i < N; ++i) {
        s_u32 kids[1] = {0};
        nodes[i] = arr_with_kids(h, kids, 1);
        if (i > 0) {
            CHECK_EQ_INT((int)s_om_write_u32(h, nodes[i], 0, nodes[i - 1]),
                         S_OM_OK);
        }
    }
    s_u32 stray = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, nodes[N - 1]), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* only stray reclaimed */
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
    s_u32 a = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, 0xFFFFFFu), S_OM_OK); /* stale */
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0);
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_repeated_collect(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_u32 a = arr_with_kids(h, NULL, 0);
    s_u32 x = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* x reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, x), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 0); /* stable */
    CHECK_EQ_INT((int)s_om_validate(h, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_live_objects(h), 1);
    s_om_gc_destroy(g);
}

static void test_freelist_reuse_after_collect(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_u32 root = arr_with_kids(h, NULL, 0);
    s_u32 junk = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, root), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* junk reclaimed */
    /* realloc a same-kind object; the allocator should reuse junk's slot */
    s_u32 revived = arr_with_kids(h, NULL, 0);
    CHECK_EQ_INT((int)revived, (int)junk);
    CHECK_EQ_INT((int)s_om_validate(h, root), S_OM_OK);
    s_om_gc_destroy(g);
}

static void test_collect_preserves_content(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_u32 b = arr_with_kids(h, NULL, 0);
    s_u32 a = 0;
    { s_u32 k[1] = {b}; a = arr_with_kids(h, k, 1); }
    s_u32 junk = arr_with_kids(h, NULL, 0);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_add_root(g, a), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1);
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);
    s_u32 child = 0;
    CHECK_EQ_INT((int)s_om_read_u32(h, a, 0, &child), S_OM_OK);
    CHECK_EQ_INT((int)child, (int)b);
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
TEST_MAIN_END()
