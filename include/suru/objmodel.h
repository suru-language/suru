#ifndef SURU_OBJMODEL_H
#define SURU_OBJMODEL_H

#include "suru/base.h"
#include "suru/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* SURU-owned object model & allocator (Phase 3E / roadmap stages 1-7) */
/*                                                                     */
/* A self-contained object model and managed allocator built directly   */
/* on the trusted platform interface (platform_mem). Objects live in    */
/* RawBlock regions; identity is a flat object index (u32), not a raw   */
/* address. Metadata (slot table) lives in a dedicated RawBlock. The    */
/* allocator uses segregated small-bucket freelists plus a bump region.  */
/* This module never triggers GC and is trusted-runtime-only.           */
/* ------------------------------------------------------------------ */

typedef s_u32 s_om_heap;

typedef enum s_om_err {
    S_OM_OK = 0,
    S_OM_EINVAL,     /* invalid argument (e.g. bad kind) */
    S_OM_ENOENT,     /* invalid/stale/freed heap or object handle */
    S_OM_EOOB,       /* payload access outside the object bounds */
    S_OM_EOVERFLOW,  /* arithmetic overflow in size/count computation */
    S_OM_ENOMEM,     /* heap region exhausted or allocation failed */
    S_OM_ECOLLECT,   /* operation forbidden while a collection is in progress */
} s_om_err;

/*
 * Create a heap owning up to `max_objects` object slots and `obj_bytes`
 * usable bytes of object payload. The heap acquires two RawBlocks: one for
 * the slot registry (bookkeeping) and one for object bytes. These are raw
 * platform regions; the object model never allocates from a managed heap.
 *
 * Pre : max_objects in [1, S_OM_MAX_OBJECTS]; obj_bytes >= header size;
 *       `out` points to a writable s_om_heap.
 * Post: on success the heap is live and `*out` holds its opaque handle; on
 *       failure no heap is created and `*out` is 0.
 */
s_om_err s_om_heap_create(s_u32 max_objects, s_size obj_bytes, s_om_heap *out);

/* Destroy a heap, releasing its RawBlocks. Subsequent use of the handle
 * fails with S_OM_ENOENT. */
s_om_err s_om_heap_destroy(s_om_heap h);
s_om_err s_om_heap_validate(s_om_heap h);

/*
 * Stop-the-world collection lock (trusted runtime only). While a collection is
 * active (lock on), structural mutations of the heap are forbidden: object
 * allocation and free fail with S_OM_ECOLLECT, and the GC's own root add/remove
 * are refused. This makes incremental marking safe by precluding graph mutation
 * during collection. Ordinary payload reads are unaffected; payload writes
 * during collection are also part of the prohibited (unwritten) mutation set
 * and must not occur under the stop-the-world contract.
 */
s_om_err s_om_set_collection_lock(s_om_heap h, bool on);
bool s_om_is_collecting(s_om_heap h);

/*
 * Allocate an object of `kind` with `count` units and `payload_bytes` bytes
 * of string payload (only meaningful for S_OBJ_STR). On success `*out_obj`
 * receives the object index (1-based; 0 is never a valid object).
 *
 * For STR, payload_bytes is the byte length and count == payload_bytes. For
 * array/record/closure, count is the number of child s_value slots and
 * payload_bytes is ignored (derived). Exception carries 3 child slots.
 *
 * The object is zero-filled, colour is S_GC_WHITE, and its slot magic is
 * set. Failure (region exhausted, fault injection, overflow) leaves existing
 * objects intact and returns an error without registering a partial object.
 */
s_om_err s_om_alloc(s_om_heap h, s_obj_kind kind, s_u32 count,
                    s_size payload_bytes, s_u32 *out_obj);

/* Free an object, returning its slot to the segregated freelist for its size
 * class. Double-free/stale handles fail with S_OM_ENOENT. In debug builds the
 * payload is poisoned. */
s_om_err s_om_free(s_om_heap h, s_u32 obj);

/* Validate a handle: ok iff the heap is live, the index is in range, and its
 * slot magic is set. */
s_om_err s_om_validate(s_om_heap h, s_u32 obj);

/*
 * Iterate live objects. Pass a cursor (u32) that starts at 0. On success the
 * cursor and *out_obj are set to the next live object index and S_OM_OK is
 * returned. When no further live object exists, S_OM_ENOENT is returned and
 * the cursor is left past the last slot. The iteration is a snapshot-free
 *  strictly-forward scan; freeing the just-returned object during iteration
 *  is permitted (later slots are unaffected).
 */
s_om_err s_om_next_live(s_om_heap h, s_u32 *cursor, s_u32 *out_obj);

/* Introspection. */
s_om_err s_om_kind(s_om_heap h, s_u32 obj, s_obj_kind *out);
s_om_err s_om_count(s_om_heap h, s_u32 obj, s_u32 *out);
s_om_err s_om_size(s_om_heap h, s_u32 obj, s_size *out);
s_om_err s_om_colour(s_om_heap h, s_u32 obj, s_u8 *out);
s_om_err s_om_set_colour(s_om_heap h, s_u32 obj, s_u8 colour);

/* Payload access. Offsets are relative to the object payload (byte 0 is the
 * first payload byte after the header). Bounds are checked against the rounded
 * object size; out-of-range access fails with S_OM_EOOB. */
s_om_err s_om_read(s_om_heap h, s_u32 obj, s_size off, void *dst, s_size len);
s_om_err s_om_write(s_om_heap h, s_u32 obj, s_size off,
                    const void *src, s_size len);
s_om_err s_om_read_u8(s_om_heap h, s_u32 obj, s_size off, s_u8 *out);
s_om_err s_om_read_u32(s_om_heap h, s_u32 obj, s_size off, s_u32 *out);
s_om_err s_om_read_u64(s_om_heap h, s_u32 obj, s_size off, s_u64 *out);
s_om_err s_om_write_u8(s_om_heap h, s_u32 obj, s_size off, s_u8 v);
s_om_err s_om_write_u32(s_om_heap h, s_u32 obj, s_size off, s_u32 v);
s_om_err s_om_write_u64(s_om_heap h, s_u32 obj, s_size off, s_u64 v);

/* Stats + fault injection (mirrors platform_mem conventions). */
s_u64 s_om_live_bytes(s_om_heap h);
s_u32 s_om_live_objects(s_om_heap h);
s_u32 s_om_active_count(void);
void s_om_dispose_all(void);
void s_om_set_fail_alloc(bool enabled);
void s_om_set_alloc_fail_after(s_u32 n_ok);

/* Limits. */
#define S_OM_MAX_HEAPS 32u
#define S_OM_MAX_OBJECTS 65536u
#define S_OM_ALIGN 16u
#define S_OM_HEADER_SIZE 16u
#define S_OM_SMALL_MAX 4096u
/* One bucket per 16-byte size class up to and including S_OM_SMALL_MAX. */
#define S_OM_SMALL_BUCKETS (S_OM_SMALL_MAX / S_OM_ALIGN + 1u)

#ifdef __cplusplus
}
#endif

#endif /* SURU_OBJMODEL_H */
