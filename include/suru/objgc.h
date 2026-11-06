#ifndef SURU_OBJGC_H
#define SURU_OBJGC_H

#include "suru/base.h"
#include "suru/objmodel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Mark-sweep collector for the SURU-owned object model (roadmap       */
/* stages 5-8: roots, traversal, marking, sweeping/reclamation).       */
/*                                                                     */
/* This is a standalone, additive collector that operates entirely on   */
/* the public object-model API. It never allocates from a managed heap  */
/* (root bookkeeping is plain C memory). Marking uses tri-color state    */
/* in the object slots and a bounded, non-recursive scan (no explicit   */
/* unbounded worklist), satisfying the approved collector design.       */
/*                                                                     */
/* Child-reference encoding used by this collector: for ARRAY/RECORD/   */
/* CLOSURE/EXCEPTION objects, payload word i (byte offset i*4) holds     */
/* the child object index for child i (0 = none). count is the number    */
/* of children. STR objects have no children. This encoding is the      */
/* graph model for this collector; the mapping to the future value-layer */
/* reference representation is a later, separate transition.            */
/* ------------------------------------------------------------------ */

typedef struct s_om_gc s_om_gc;

/* Bind a collector to a heap. `out` receives the collector handle. */
s_om_err s_om_gc_create(s_om_heap h, s_om_gc **out);
void s_om_gc_destroy(s_om_gc *g);

/* Root management. Roots are object handles; they are validated lazily at
 * collect time (an invalid/stale root is skipped, not an error). */
s_om_err s_om_gc_add_root(s_om_gc *g, s_u32 obj);
s_om_err s_om_gc_remove_root(s_om_gc *g, s_u32 obj);
void s_om_gc_clear_roots(s_om_gc *g);
s_u32 s_om_gc_nroots(const s_om_gc *g);

/*
 * Run a full mark-sweep collection. Returns the number of objects reclaimed.
 * Live objects reachable from the roots are preserved and reset to the white
 * colour for the next cycle. Unreachable live objects are freed back to the
 * allocator's freelists.
 */
s_u32 s_om_gc_collect(s_om_gc *g);

#ifdef __cplusplus
}
#endif

#endif /* SURU_OBJGC_H */
