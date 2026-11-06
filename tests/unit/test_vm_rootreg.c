#include "test_framework.h"
#include "suru/bc.h"
#include "suru/objmodel.h"
#include "suru/objgc.h"
#include "suru/vm.h"
#include "suru/platform_mem.h"

#include <stdlib.h>
#include <string.h>

static void om_reset(void) { s_om_dispose_all(); s_pm_dispose_all(); }

static char *xdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

static void init_strings(s_bc_module *m) {
    m->nstrings = 1;
    m->strings = calloc(1, sizeof(char *));
    m->strlens = calloc(1, sizeof(s_u32));
    m->strings[0] = xdup("S");
    m->strlens[0] = 1;
}

static s_bc_module tiny_module(void) {
    s_bc_module m;
    s_bc_module_init(&m);
    m.entry = 0;
    init_strings(&m);
    m.nints = 1;
    m.ints = calloc(1, sizeof(s_i64));
    m.ints[0] = 0;
    m.nfuncs = 1;
    m.funcs = calloc(1, sizeof(s_bc_func));
    s_bc_func *f = &m.funcs[0];
    f->name = xdup("main");
    f->frame_size = 0;
    f->max_operand_depth = 2;
    f->nins = 1;
    f->ins = calloc(1, sizeof(s_bc_ins));
    f->ins[0] = (s_bc_ins){ BC_RET, 0, 0, 0 };
    return m;
}

static s_om_ref om_arr(s_om_heap h) {
    s_om_ref o = S_OM_REF_NONE;
    CHECK_EQ_INT((int)s_om_alloc(h, S_OBJ_ARRAY, 0, 0, &o), S_OM_OK);
    return o;
}

/* Stage 11C: VM bind/sync/unbind plumbing against a real (legacy) VM. */
static void test_vm_bind_sync_unbind(void) {
    om_reset();
    s_bc_module m = tiny_module();
    s_vm *vm = s_vm_create(&m);
    CHECK(vm != NULL);

    s_om_heap oh = 0;
    CHECK_EQ_INT((int)s_om_heap_create(64, 4096, &oh), S_OM_OK);
    s_om_gc *gc = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(oh, &gc), S_OM_OK);
    (void)om_arr(oh);

    /* sync without a bound collector is a harmless no-op */
    CHECK_EQ_INT((int)s_vm_gc_sync(vm), S_OM_OK);

    s_vm_gc_bind(vm, gc);
    CHECK_EQ_INT((int)s_vm_gc_sync(vm), S_OM_OK); /* empty stack safe */

    s_vm_gc_unbind(vm);
    CHECK_EQ_INT((int)s_vm_gc_sync(vm), S_OM_OK);

    s_vm_destroy(vm); /* must invalidate registration before freeing stack */
    s_om_gc_destroy(gc);
    s_bc_module_dispose(&m);
}

/* Value-aware roots: only genuine S_VAL_OBJ entries become roots; an immediate
 * whose bit pattern equals a handle must NOT preserve that object. */
static void test_svalue_root_filters_nonobj(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref x = om_arr(h);   /* "seen" only via an INT bit pattern */
    s_om_ref keep = om_arr(h); /* real OBJ root */
    s_om_ref junk = om_arr(h);

    s_value sv[4];
    sv[0].tag = S_VAL_INT;
    sv[0].u.i = (s_i64)x;              /* trap: int bits equal x's handle */
    sv[1] = s_val_obj_h((s_u64)keep);  /* genuine object handle */
    sv[2].tag = S_VAL_NONE;
    sv[2].u.i = 0;
    sv[3].tag = S_VAL_FLOAT;
    sv[3].u.f = 1.0;

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_set_svalue_root(g, sv, 4), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 2); /* x and junk reclaimed */
    CHECK_EQ_INT((int)s_om_validate(h, keep), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, x), S_OM_ENOENT);
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);
    s_om_gc_destroy(g);
}

/* Stack relocation: the VM's grow_stack uses realloc, which can move the buffer.
 * The collector must never be left holding a stale base after a move; the
 * contract is that the VM re-registers after every growth. */
static void test_stack_root_relocation(void) {
    om_reset();
    s_om_heap h = 0;
    CHECK_EQ_INT((int)s_om_heap_create(128, 65536, &h), S_OM_OK);
    s_om_ref keep = om_arr(h);
    s_om_ref junk = om_arr(h);

    s_om_gc *g = NULL;
    CHECK_EQ_INT((int)s_om_gc_create(h, &g), S_OM_OK);

    s_om_ref *buf = malloc(4 * sizeof(s_om_ref));
    CHECK(buf != NULL);
    buf[0] = keep;
    CHECK_EQ_INT((int)s_om_gc_set_stack_root(g, buf, 1), S_OM_OK);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* junk reclaimed; keep survives */
    CHECK_EQ_INT((int)s_om_validate(h, junk), S_OM_ENOENT);

    /* simulate grow_stack relocation */
    s_om_ref *nbuf = realloc(buf, 64 * sizeof(s_om_ref));
    CHECK(nbuf != NULL);
    if (nbuf != buf) {
        CHECK_EQ_INT((int)s_om_gc_set_stack_root(g, nbuf, 1), S_OM_OK);
    }
    buf = nbuf;
    buf[0] = keep;
    s_om_ref junk2 = om_arr(h);
    CHECK_EQ_INT((int)s_om_gc_collect(g), 1); /* junk2 reclaimed; keep survives */
    CHECK_EQ_INT((int)s_om_validate(h, keep), S_OM_OK);
    CHECK_EQ_INT((int)s_om_validate(h, junk2), S_OM_ENOENT);

    free(buf);
    s_om_gc_destroy(g);
}

TEST_MAIN_BEGIN()
    test_vm_bind_sync_unbind();
    test_svalue_root_filters_nonobj();
    test_stack_root_relocation();
TEST_MAIN_END()
