#ifndef SURU_BC_H
#define SURU_BC_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Instruction set                                                     */
/* ------------------------------------------------------------------ */

typedef enum s_bc_op {
    BC_NOP = 0,

    BC_CONST_INT,      /* a = int constant index */
    BC_CONST_FLOAT,    /* a = float constant index */
    BC_CONST_STR,      /* a = string constant index */
    BC_CONST_NONE,     /* push none */

    BC_LD_LOCAL,       /* a = local slot */
    BC_ST_LOCAL,       /* a = local slot, pops value */

    BC_POP,
    BC_DUP,
    BC_SWAP,

    BC_NOT,            /* pops bool, pushes !bool */

    BC_NEG_I,
    BC_NEG_F,

    BC_ADD_I, BC_SUB_I, BC_MUL_I, BC_DIV_I, BC_MOD_I,
    BC_ADD_F, BC_SUB_F, BC_MUL_F, BC_DIV_F,

    BC_EQ_I, BC_NE_I, BC_LT_I, BC_LE_I, BC_GT_I, BC_GE_I,
    BC_EQ_F, BC_NE_F, BC_LT_F, BC_LE_F, BC_GT_F, BC_GE_F,
    BC_EQ_S, BC_NE_S, BC_LT_S, BC_LE_S, BC_GT_S, BC_GE_S,
    BC_CONCAT_S,           /* pop b, a (str); push a concatenated with b */
    BC_EQ_B, BC_NE_B,

    BC_NEW_ARRAY,      /* a = element count, pops that many values, pushes array */
    BC_NEW_RECORD,     /* a = record type index, b = field count; pops fields */

    BC_IDX_LD,         /* pops index, array; pushes element */
    BC_IDX_ST,         /* pops value, index, array; stores */
    BC_ARR_LEN,        /* pops array; pushes element count as int */

    BC_FIELD_LD,       /* a = record type index, b = field index; pops record */
    BC_FIELD_ST,       /* a = record type index, b = field index; pops value, record */

    BC_CALL,           /* a = function index; pops nargs, pushes result */
    BC_BUILTIN,        /* a = builtin id */

    BC_RET,            /* pops result, returns */
    BC_RET_UNIT,       /* returns unit */

    BC_JMP,            /* a = target instruction index */
    BC_JZ,             /* a = target; pops bool, jumps if false */
    BC_JNZ,            /* a = target; pops bool, jumps if true */

    BC_TRAP,           /* abort */

    BC_OP_COUNT
} s_bc_op;

typedef struct s_bc_ins {
    s_u8 op;
    s_u32 a;
    s_u32 b;
    s_u32 c;
} s_bc_ins;

/* Opcode metadata. */
typedef struct s_bc_op_info {
    const char *name;
    s_u8 noperands;   /* 0..3 */
    s_u8 pops;         /* operands popped (for stack-accounting) */
    s_u8 pushes;       /* operands pushed */
    s_u8 is_branch;
    s_u8 is_terminal;  /* ends control flow (ret/trap) */
} s_bc_op_info;

extern const s_bc_op_info s_bc_op_table[BC_OP_COUNT];

/* ------------------------------------------------------------------ */
/* Module                                                              */
/* ------------------------------------------------------------------ */

typedef struct s_bc_func {
    char *name;
    s_u32 nparams;
    s_u32 frame_size;          /* local slots (>= nparams) */
    s_u32 max_operand_depth;
    s_bc_ins *ins;
    s_u32 nins;
} s_bc_func;

typedef struct s_bc_module {
    s_u32 version;
    s_u32 entry;               /* main function index */

    s_i64 *ints;
    s_u32 nints;
    double *floats;
    s_u32 nfloats;
    char **strings;
    s_u32 *strlens;
    s_u32 nstrings;

    s_u32 *record_nfields;
    const char **record_names;
    s_u32 nrecords;

    s_bc_func *funcs;
    s_u32 nfuncs;
} s_bc_module;

#define S_BC_MAGIC "SURU"
#define S_BC_VERSION 1u
#define S_BC_MAX_FRAME 256u
#define S_BC_MAX_DEPTH 1024u

/* Create an empty module (caller fills and disposes). */
void s_bc_module_init(s_bc_module *m);
void s_bc_module_dispose(s_bc_module *m);

/* Serialize the module to a freshly allocated buffer (LE, arch-independent).
 * Returns 0 on success, negative on error. On success *out and *outlen are
 * set and the caller must free(*out). */
int s_bc_write(const s_bc_module *m, s_u8 **out, s_size *outlen);

/* Deserialize. Returns 0 on success; on error returns negative and leaves
 * *out untouched. The caller must s_bc_module_dispose on success. */
int s_bc_read(const s_u8 *buf, s_size buflen, s_bc_module *out);

#ifdef __cplusplus
}
#endif

#endif /* SURU_BC_H */
