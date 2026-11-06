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
/* Handle-based object value (Stage 11D-A): carries a generation-protected
 * s_om_ref in u.idx instead of a raw s_gcobj* pointer. The handle is the
 * complete 64-bit identity: (generation << 32) | slot. A handle denotes
 * identity, never an address. The tag S_VAL_OBJ is the only object tag;
 * non-object values are unaffected. */
static inline s_value s_val_obj_h(s_u64 h) { s_value x; x.tag = S_VAL_OBJ; x.u.idx = h; return x; }

/* Full 64-bit handle accessors. Only valid for S_VAL_OBJ values. Never
 * truncate the handle to 32 bits and never reinterpret it as a pointer. */
static inline s_u64 s_val_obj_handle(s_value v) { return v.u.idx; }
static inline s_u64 s_val_obj_id(s_value v) { return v.u.idx; }

/* Build a handle value from explicit slot + generation parts. */
static inline s_value s_val_obj_h_parts(s_u32 slot, s_u32 gen) {
    s_value x;
    x.tag = S_VAL_OBJ;
    x.u.idx = ((s_u64)gen << 32) | (s_u64)slot;
    return x;
}

/* Decode a handle value. Only valid for S_VAL_OBJ values. */
static inline s_u32 s_val_obj_slot(s_value v) { return (s_u32)(v.u.idx & 0xFFFFFFFFu); }
static inline s_u32 s_val_obj_generation(s_value v) { return (s_u32)(v.u.idx >> 32); }
static inline s_u64 s_val_obj_zero(void) { return 0ULL; }

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
