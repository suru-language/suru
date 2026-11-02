#include "test_framework.h"
#include "suru/bc.h"
#include "suru/verify.h"

static char *xdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

static s_bc_module make_valid_module(void) {
    s_bc_module m;
    s_bc_module_init(&m);
    m.version = S_BC_VERSION;
    m.entry = 0;
    m.nints = 2;
    m.ints = malloc(2 * sizeof(s_i64));
    m.ints[0] = 0;
    m.ints[1] = 1;
    m.nfuncs = 1;
    m.funcs = calloc(1, sizeof(s_bc_func));
    s_bc_func *f = &m.funcs[0];
    f->name = xdup("main");
    f->nparams = 0;
    f->frame_size = 2;
    f->max_operand_depth = 3;
    f->nins = 6;
    f->ins = calloc(6, sizeof(s_bc_ins));
    f->ins[0] = (s_bc_ins){ BC_CONST_INT, 0, 0, 0 };
    f->ins[1] = (s_bc_ins){ BC_ST_LOCAL, 0, 0, 0 };
    f->ins[2] = (s_bc_ins){ BC_LD_LOCAL, 0, 0, 0 };
    f->ins[3] = (s_bc_ins){ BC_CONST_INT, 1, 0, 0 };
    f->ins[4] = (s_bc_ins){ BC_ADD_I, 0, 0, 0 };
    f->ins[5] = (s_bc_ins){ BC_RET, 0, 0, 0 };
    return m;
}

static void test_verify_valid(void) {
    s_bc_module m = make_valid_module();
    CHECK_EQ_INT(s_verify_module(&m), 0);
    s_bc_module_dispose(&m);
}

static void test_roundtrip(void) {
    s_bc_module m = make_valid_module();
    m.nstrings = 1;
    m.strings = malloc(sizeof(char *));
    m.strlens = malloc(sizeof(s_u32));
    m.strings[0] = xdup("abc");
    m.strlens[0] = 3;    s_u8 *buf = NULL;
    s_size len = 0;
    CHECK_EQ_INT(s_bc_write(&m, &buf, &len), 0);
    CHECK(buf != NULL);
    CHECK(len > 0);
    s_bc_module m2;
    CHECK_EQ_INT(s_bc_read(buf, len, &m2), 0);
    CHECK_EQ_INT(m2.version, S_BC_VERSION);
    CHECK_EQ_INT(m2.entry, 0);
    CHECK_EQ_INT(m2.nfuncs, 1);
    CHECK_EQ_INT(m2.funcs[0].nins, 6);
    CHECK_EQ_INT(m2.funcs[0].ins[4].op, BC_ADD_I);
    CHECK_EQ_INT(m2.nstrings, 1);
    CHECK_STR_EQ(m2.strings[0], "abc");
    s_bc_module_dispose(&m2);
    free(buf);
    s_bc_module_dispose(&m);
}

static void test_verify_bad_jump(void) {
    s_bc_module m = make_valid_module();
    m.funcs[0].ins[5].op = BC_JMP;
    m.funcs[0].ins[5].a = 999;
    /* now the function ends with a jump to an invalid target; verifier rejects */
    CHECK(s_verify_module(&m) != 0);
    s_bc_module_dispose(&m);
}

static void test_verify_underflow(void) {
    s_bc_module m = make_valid_module();
    /* replace everything with a single RET at depth 0 */
    m.funcs[0].nins = 1;
    m.funcs[0].ins[0] = (s_bc_ins){ BC_RET, 0, 0, 0 };
    CHECK(s_verify_module(&m) != 0);
    s_bc_module_dispose(&m);
}

static void test_verify_bad_local(void) {
    s_bc_module m = make_valid_module();
    m.funcs[0].ins[2].a = 99; /* LD_LOCAL 99 with frame_size 2 */
    CHECK(s_verify_module(&m) != 0);
    s_bc_module_dispose(&m);
}

static void test_read_garbage(void) {
    s_u8 garbage[64];
    for (int i = 0; i < 64; i++) garbage[i] = (s_u8)(i * 7);
    s_bc_module m;
    CHECK(s_bc_read(garbage, 64, &m) != 0);
    /* also truncated magic */
    CHECK(s_bc_read(garbage, 2, &m) != 0);
}

TEST_MAIN_BEGIN()
    test_verify_valid();
    test_roundtrip();
    test_verify_bad_jump();
    test_verify_underflow();
    test_verify_bad_local();
    test_read_garbage();
TEST_MAIN_END()
