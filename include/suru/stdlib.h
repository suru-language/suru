#ifndef SURU_STDLIB_H
#define SURU_STDLIB_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Standard library builtins. These are resolved by name in the frontend and
 * dispatched by id in the VM. The signatures here are the single source of
 * truth shared by the semantic analyzer, the bytecode compiler, the verifier,
 * and the runtime. */

typedef enum s_builtin_id {
    S_BUILTIN_PRINT = 0,
    S_BUILTIN_PRINTLN,
    S_BUILTIN_STR_LEN,
    S_BUILTIN_ARRAY_LEN,
    S_BUILTIN_READ_LINE,
    S_BUILTIN_TO_STR,
    S_BUILTIN_F64_TO_STR,
    S_BUILTIN_I64_TO_F64,
    S_BUILTIN_F64_TO_I64,
    S_BUILTIN_MAKE_ARRAY,
    S_BUILTIN_RANGE,
    S_BUILTIN_SUBSTR,
    S_BUILTIN_STR_CONTAINS,
    S_BUILTIN_INT_FROM_STR,
    S_BUILTIN_ABS,
    S_BUILTIN_MIN,
    S_BUILTIN_MAX,
    S_BUILTIN_STR_TO_F64,
    S_BUILTIN_RANGE_STEP,
    S_BUILTIN_ASSERT,
    S_BUILTIN_EXIT,
    S_BUILTIN_TO_HEX,
    S_BUILTIN_TO_BIN,
    S_BUILTIN_F64_TO_STR_PREC,
    S_BUILTIN_COUNT
} s_builtin_id;

typedef enum s_sig_type {
    S_SIG_UNIT = 0,
    S_SIG_INT,
    S_SIG_FLOAT,
    S_SIG_BOOL,
    S_SIG_STR,
    S_SIG_ARRAY_INT,
    S_SIG_ARRAY,          /* any array, regardless of element type */
    S_SIG_ANY,            /* any value */
} s_sig_type;

typedef struct s_builtin_sig {
    const char *name;
    s_u8 nparams;
    s_sig_type params[3];
    s_sig_type ret;
} s_builtin_sig;

extern const s_builtin_sig s_builtin_table[S_BUILTIN_COUNT];

/* Find a builtin by name; returns NULL if not a builtin. */
const s_builtin_sig *s_builtin_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SURU_STDLIB_H */
