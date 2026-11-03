#include "test_framework.h"
#include "suru/bc.h"
#include "suru/mem.h"
#include "suru/vm.h"

static char *xdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

static void init_strings(s_bc_module *m) {
    m->nstrings = 2;
    m->strings = calloc(2, sizeof(char *));
    m->strlens = calloc(2, sizeof(s_u32));
    m->strings[0] = xdup("FileError");
    m->strings[1] = xdup("read failed");
    m->strlens[0] = 9;
    m->strlens[1] = 11;
}

static s_bc_module caught_module(void) {
    s_bc_module m;
    s_bc_module_init(&m);
    m.entry = 0;
    init_strings(&m);
    m.nints = 1;
    m.ints = calloc(1, sizeof(s_i64));
    m.ints[0] = 7;
    m.nfuncs = 1;
    m.funcs = calloc(1, sizeof(s_bc_func));
    s_bc_func *f = &m.funcs[0];
    f->name = xdup("main");
    f->frame_size = 0;
    f->max_operand_depth = 3;
    f->nins = 11;
    f->ins = calloc(f->nins, sizeof(s_bc_ins));
    f->ins[0] = (s_bc_ins){ BC_PUSH_HANDLER, 8, 0, 0 };
    f->ins[1] = (s_bc_ins){ BC_CONST_STR, 0, 0, 0 };
    f->ins[2] = (s_bc_ins){ BC_CONST_STR, 1, 0, 0 };
    f->ins[3] = (s_bc_ins){ BC_CONST_NONE, 0, 0, 0 };
    f->ins[4] = (s_bc_ins){ BC_NEW_EXCEPTION, 0, 0, 0 };
    f->ins[5] = (s_bc_ins){ BC_THROW, 0, 0, 0 };
    f->ins[8] = (s_bc_ins){ BC_POP, 0, 0, 0 };
    f->ins[9] = (s_bc_ins){ BC_CONST_INT, 0, 0, 0 };
    f->ins[10] = (s_bc_ins){ BC_RET, 0, 0, 0 };
    return m;
}

static void test_caught_exception_unwinds(void) {
    s_bc_module m = caught_module();
    s_heap *heap = s_heap_create();
    s_vm *vm = s_vm_create(&m);
    CHECK(vm != NULL);
    CHECK_EQ_INT(s_vm_run(vm, heap), 7);
    CHECK(s_vm_trapped(vm) == false);
    s_vm_destroy(vm);
    s_heap_destroy(heap);
    s_bc_module_dispose(&m);
}

TEST_MAIN_BEGIN()
    test_caught_exception_unwinds();
TEST_MAIN_END()
