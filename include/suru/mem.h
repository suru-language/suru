#ifndef SURU_MEM_H
#define SURU_MEM_H

#include "suru/base.h"
#include "suru/value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* GC object                                                           */
/* ------------------------------------------------------------------ */

typedef enum s_obj_kind {
    S_OBJ_STR = 0,
    S_OBJ_ARRAY = 1,
    S_OBJ_RECORD = 2,
    S_OBJ_CLOSURE = 3,
} s_obj_kind;

struct s_gcobj {
    s_size chunk_size;         /* total allocated bytes incl header */
    struct s_gcobj *next_all;  /* intrusive live-list link */
    s_u32 count;               /* str: byte length; array/record/closure: child Value count */
    s_u32 magic;               /* sanity tag */
    s_u8 kind;
    s_u8 marked;
    s_u16 flags;
};

#define S_OBJ_MAGIC 0x5355524Fu  /* "SURU" */

/* Payload accessors (memory follows the header, 8-aligned). */
static inline char *s_obj_str_data(s_gcobj *o) { return (char *)(o + 1); }
static inline s_value *s_obj_array_data(s_gcobj *o) { return (s_value *)(o + 1); }
static inline s_value *s_obj_record_fields(s_gcobj *o) { return (s_value *)(o + 1); }

static inline s_size s_obj_str_len(s_gcobj *o) { return (s_size)o->count; }

/* ------------------------------------------------------------------ */
/* Heap                                                                */
/* ------------------------------------------------------------------ */

typedef struct s_heap s_heap;

s_heap *s_heap_create(void);
void s_heap_destroy(s_heap *h);

/* Allocate an object. kind determines payload layout. For S_OBJ_STR,
 * payload_bytes is the string length; for array/record/closure, count is the
 * number of child Value slots and payload_bytes may be 0 (derived). Returns
 * NULL on allocation failure. The object's memory is zero-filled. */
s_gcobj *s_heap_alloc(s_heap *h, s_obj_kind kind, s_u32 count, s_size payload_bytes);

s_gcobj *s_heap_new_str(s_heap *h, const char *bytes, s_size len);
s_gcobj *s_heap_new_array(s_heap *h, s_u32 count);
s_gcobj *s_heap_new_record(s_heap *h, s_u32 nfields);

/* Roots. The runtime registers the live region of its stack as the primary
 * root range, and may register additional single slots. */
void s_heap_set_stack_root(s_heap *h, s_value *base, s_size len);
void s_heap_add_root(s_heap *h, s_value *slot);
void s_heap_remove_root(s_heap *h, s_value *slot);

void s_heap_collect(s_heap *h);

s_u64 s_heap_live_bytes(const s_heap *h);
s_u64 s_heap_total_bytes(const s_heap *h);
s_u32 s_heap_live_objects(const s_heap *h);
s_u64 s_heap_collections(const s_heap *h);

/* Fault injection: when enabled, allocation returns NULL. */
void s_heap_set_fail_alloc(s_heap *h, bool enabled);
void s_heap_set_alloc_fail_after(s_heap *h, s_u32 n_ok);

#ifdef __cplusplus
}
#endif

#endif /* SURU_MEM_H */
