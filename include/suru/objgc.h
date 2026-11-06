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
/* CLOSURE/EXCEPTION objects, payload word i (byte offset i*8) holds     */
/* the child OBJECT HANDLE (s_om_ref) for child i (0 = none). count is   */
/* the number of children. STR objects have no children. This encoding   */
/* is the graph model for this collector; the mapping to the future      */
/* value-layer reference representation is a later, separate transition. */
/* ------------------------------------------------------------------ */

typedef struct s_om_gc s_om_gc;

/* Bind a collector to a heap. `out` receives the collector handle. */
s_om_err s_om_gc_create(s_om_heap h, s_om_gc **out);
void s_om_gc_destroy(s_om_gc *g);

/* Root management. Roots are full managed object handles (s_om_ref); they are
 * validated lazily at collect time (an invalid/stale root is skipped, not an
 * error). Root add/remove are forbidden while a collection is active. */
s_om_err s_om_gc_add_root(s_om_gc *g, s_om_ref obj);
s_om_err s_om_gc_remove_root(s_om_gc *g, s_om_ref obj);
void s_om_gc_clear_roots(s_om_gc *g);
s_u32 s_om_gc_nroots(const s_om_gc *g);

/*
 * Register a contiguous range of managed handles as roots (borrowed, not
 * copied), mirroring s_heap_set_stack_root for the VM's operand stack. The
 * range must stay valid and unchanged until the next full/incremental
 * collection completes (stop-the-world). Passing base==NULL with n==0 clears
 * the range. Stale/invalid handles in the range are ignored at mark time.
 */
s_om_err s_om_gc_set_stack_root(s_om_gc *g, const s_om_ref *base, s_size n);

/*
 * Run a full mark-sweep collection. Returns the number of objects reclaimed.
 * Live objects reachable from the roots are preserved and reset to the white
 * colour for the next cycle. Unreachable live objects are freed back to the
 * allocator's freelists.
 *
 * Equivalent to mark_begin + repeated mark_step until marking completes +
 * sweep.
 */
s_u32 s_om_gc_collect(s_om_gc *g);

/* Incremental marking. The marking phase can be budget-sliced across many
 * calls; each step examines at most `budget` live objects. The scan is a
 * bounded, non-recursive fixpoint over the slot table (no unbounded
 * worklist). Returns true while marking is still in progress, false when it
 * has completed (a full pass with no newly-shaded objects). Sweep after
 * marking completes. */
s_om_err s_om_gc_mark_begin(s_om_gc *g);
bool s_om_gc_mark_step(s_om_gc *g, s_size budget);
s_u32 s_om_gc_sweep(s_om_gc *g);
bool s_om_gc_marking(const s_om_gc *g);

/*
 * Stop-the-world lifecycle contract. From mark_begin until marking completes,
 * the collector takes the heap's collection lock (see s_om_set_collection_lock)
 * and the following are forbidden and fail with S_OM_ECOLLECT:
 *   - object allocation and free (s_om_alloc / s_om_free),
 *   - root add/remove (s_om_gc_add_root / s_om_gc_remove_root).
 * Payload writes to objects during marking are likewise prohibited by the
 * contract (no write barrier is provided); violating this can leave a black
 * object pointing at a white object that is later reclaimed. A new collection
 * cannot start while one is active (mark_begin returns S_OM_ECOLLECT), and
 * sweep must only be called after marking completes (it is a no-op otherwise).
 */

#ifdef __cplusplus
}
#endif

#endif /* SURU_OBJGC_H */
