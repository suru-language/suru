#ifndef SURU_VALUE_H
#define SURU_VALUE_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime values. Immediates (int, float, bool, none) are stored directly;
 * heap data (str, array, record, closure) is referenced through a tagged
 * pointer to a GC object. */

typedef enum s_value_tag {
    S_VAL_INT,
    S_VAL_FLOAT,
    S_VAL_BOOL,
    S_VAL_NONE,
    S_VAL_OBJ,
} s_value_tag;

typedef struct s_gcobj s_gcobj;

typedef struct s_value {
    s_value_tag tag;
    union {
        s_i64 i;
        double f;
        bool b;
        s_gcobj *o;   /* pointer-based object reference (C-heap path) */
        s_u64 idx;    /* generation-protected object handle (objmodel path) */
    } u;
} s_value;

static inline s_value s_val_int(s_i64 v) { s_value x; x.tag = S_VAL_INT; x.u.i = v; return x; }
static inline s_value s_val_float(double v) { s_value x; x.tag = S_VAL_FLOAT; x.u.f = v; return x; }
static inline s_value s_val_bool(bool v) { s_value x; x.tag = S_VAL_BOOL; x.u.b = v; return x; }
static inline s_value s_val_none(void) { s_value x; x.tag = S_VAL_NONE; x.u.i = 0; return x; }
static inline s_value s_val_obj(s_gcobj *o) { s_value x; x.tag = S_VAL_OBJ; x.u.o = o; return x; }

static inline bool s_val_is_int(s_value v) { return v.tag == S_VAL_INT; }
static inline bool s_val_is_float(s_value v) { return v.tag == S_VAL_FLOAT; }
static inline bool s_val_is_bool(s_value v) { return v.tag == S_VAL_BOOL; }
static inline bool s_val_is_none(s_value v) { return v.tag == S_VAL_NONE; }
static inline bool s_val_is_obj(s_value v) { return v.tag == S_VAL_OBJ; }

/* Human-readable tag name (for traps/messages). */
const char *s_value_tag_name(s_value_tag t);

#ifdef __cplusplus
}
#endif

#endif /* SURU_VALUE_H */
