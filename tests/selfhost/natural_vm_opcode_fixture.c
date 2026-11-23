#include "suru/bc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_INS = 256 };

static void emit(s_bc_ins *ins, s_u32 *count, s_bc_op op,
                 s_u32 a, s_u32 b, s_u32 c) {
    ins[*count] = (s_bc_ins){ (s_u8)op, a, b, c };
    ++*count;
}

static void push_int(s_bc_ins *ins, s_u32 *count, s_u32 index) {
    emit(ins, count, BC_CONST_INT, index, 0, 0);
}

static void push_float(s_bc_ins *ins, s_u32 *count, s_u32 index) {
    emit(ins, count, BC_CONST_FLOAT, index, 0, 0);
}

/* Trap if a computed 0/1 result differs from its expected value. */
static void check_result(s_bc_ins *ins, s_u32 *count, int expected) {
    s_u32 branch = *count;
    emit(ins, count, expected ? BC_JZ : BC_JNZ, 0, 0, 0);
    s_u32 skip_trap = *count;
    emit(ins, count, BC_JMP, 0, 0, 0);
    s_u32 trap = *count;
    emit(ins, count, BC_TRAP, 0, 0, 0);
    s_u32 done = *count;
    ins[branch].a = trap;
    ins[skip_trap].a = done;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;

    s_bc_module module;
    s_bc_module_init(&module);
    module.version = S_BC_VERSION;
    module.entry = 0;

    static const s_i64 integers[] = {
        0, 1, 2, 3, 6, 7, 8, 10, 12, 15, -1
    };
    static const double floats[] = {
        1.5, 2.5, -1.5, 7.0
    };
    module.nints = (s_u32)(sizeof integers / sizeof integers[0]);
    module.ints = malloc(sizeof integers);
    module.nfloats = (s_u32)(sizeof floats / sizeof floats[0]);
    module.floats = malloc(sizeof floats);
    module.nfuncs = 1;
    module.funcs = calloc(1, sizeof *module.funcs);
    if (!module.ints || !module.floats || !module.funcs) {
        s_bc_module_dispose(&module);
        return 2;
    }
    memcpy(module.ints, integers, sizeof integers);
    memcpy(module.floats, floats, sizeof floats);

    s_bc_func *main_fn = &module.funcs[0];
    main_fn->name = malloc(sizeof "main");
    main_fn->ins = calloc(MAX_INS, sizeof *main_fn->ins);
    if (!main_fn->name || !main_fn->ins) {
        s_bc_module_dispose(&module);
        return 2;
    }
    memcpy(main_fn->name, "main", sizeof "main");

    s_u32 count = 0;
    /* BC_NEG_F and BC_CONV_I_F. */
    push_float(main_fn->ins, &count, 0);
    emit(main_fn->ins, &count, BC_NEG_F, 0, 0, 0);
    push_float(main_fn->ins, &count, 2);
    emit(main_fn->ins, &count, BC_EQ_F, 0, 0, 0);
    check_result(main_fn->ins, &count, 1);

    push_int(main_fn->ins, &count, 5);
    emit(main_fn->ins, &count, BC_CONV_I_F, 0, 0, 0);
    push_float(main_fn->ins, &count, 3);
    emit(main_fn->ins, &count, BC_EQ_F, 0, 0, 0);
    check_result(main_fn->ins, &count, 1);

    /* All six floating-point comparisons. */
#define FLOAT_CMP(op, expected) do { \
    push_float(main_fn->ins, &count, 0); \
    push_float(main_fn->ins, &count, 1); \
    emit(main_fn->ins, &count, op, 0, 0, 0); \
    check_result(main_fn->ins, &count, expected); \
} while (0)
    FLOAT_CMP(BC_EQ_F, 0);
    FLOAT_CMP(BC_NE_F, 1);
    FLOAT_CMP(BC_LT_F, 1);
    FLOAT_CMP(BC_LE_F, 1);
    FLOAT_CMP(BC_GT_F, 0);
    FLOAT_CMP(BC_GE_F, 0);
#undef FLOAT_CMP

    /* Whole bitwise operations, including shifts. */
#define INT_BINARY(op, lhs, rhs, expected) do { \
    push_int(main_fn->ins, &count, lhs); \
    push_int(main_fn->ins, &count, rhs); \
    emit(main_fn->ins, &count, op, 0, 0, 0); \
    push_int(main_fn->ins, &count, expected); \
    emit(main_fn->ins, &count, BC_EQ_I, 0, 0, 0); \
    check_result(main_fn->ins, &count, 1); \
} while (0)
    INT_BINARY(BC_BIT_AND, 8, 7, 6); /* 12 & 10 = 8 */
    INT_BINARY(BC_BIT_OR, 8, 3, 9);  /* 12 | 3 = 15 */
    INT_BINARY(BC_BIT_XOR, 8, 7, 4); /* 12 ^ 10 = 6 */
    INT_BINARY(BC_SHL, 3, 2, 8);     /* 3 << 2 = 12 */
    INT_BINARY(BC_SHR, 8, 2, 3);     /* 12 >> 2 = 3 */
#undef INT_BINARY

    push_int(main_fn->ins, &count, 0);
    emit(main_fn->ins, &count, BC_BIT_NOT, 0, 0, 0);
    push_int(main_fn->ins, &count, 10);
    emit(main_fn->ins, &count, BC_EQ_I, 0, 0, 0);
    check_result(main_fn->ins, &count, 1);

    /* BC_EQ_B / BC_NE_B compare the underlying 0/1 values. */
#define BOOL_CMP(op, lhs, rhs, expected) do { \
    push_int(main_fn->ins, &count, lhs); \
    push_int(main_fn->ins, &count, rhs); \
    emit(main_fn->ins, &count, op, 0, 0, 0); \
    check_result(main_fn->ins, &count, expected); \
} while (0)
    BOOL_CMP(BC_EQ_B, 1, 1, 1);
    BOOL_CMP(BC_NE_B, 1, 1, 0);
    BOOL_CMP(BC_EQ_B, 1, 0, 0);
    BOOL_CMP(BC_NE_B, 1, 0, 1);
#undef BOOL_CMP

    emit(main_fn->ins, &count, BC_RET_UNIT, 0, 0, 0);
    main_fn->nins = count;
    main_fn->frame_size = 0;
    main_fn->nparams = 0;
    main_fn->max_operand_depth = 2;

    s_u8 *bytes = NULL;
    s_size length = 0;
    if (s_bc_write(&module, &bytes, &length) != 0) {
        s_bc_module_dispose(&module);
        return 2;
    }
    FILE *output = fopen(argv[1], "wb");
    if (!output) {
        free(bytes);
        s_bc_module_dispose(&module);
        return 2;
    }
    int result = fwrite(bytes, 1, length, output) == length ? 0 : 2;
    if (fclose(output) != 0) result = 2;
    free(bytes);
    s_bc_module_dispose(&module);
    return result;
}
