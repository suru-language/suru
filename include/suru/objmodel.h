#ifndef SURU_OBJMODEL_H
#define SURU_OBJMODEL_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum s_obj_kind {
    S_OBJ_STR = 0,
    S_OBJ_ARRAY = 1,
    S_OBJ_RECORD = 2,
    S_OBJ_CLOSURE = 3,
    S_OBJ_EXCEPTION = 4,
} s_obj_kind;

#define S_GC_WHITE 0u
#define S_GC_GREY  1u
#define S_GC_BLACK  2u
#define S_OBJ_MAGIC 0x5355524Fu  /* "SURU" */

/* ------------------------------------------------------------------ */
/* SURU-owned object model & allocator (Phase 3E / roadmap stages 1-7) */
/*                                                                     */
/* A self-contained object model and managed allocator built directly   */
/* on the trusted platform interface (platform_mem). Objects live in    */
/* RawBlock regions; identity is a generation-protected 64-bit handle,  */
/* not a raw address and not a bare slot index. Metadata (slot table)   */
/* lives in a dedicated RawBlock. The allocator uses segregated small-  */
/* bucket freelists plus a bump region. This module never triggers GC   */
/* and is trusted-runtime-only.                                         */
/* ------------------------------------------------------------------ */

typedef s_u32 s_om_heap;

/*
 * Managed-object handle (Phase 3F.1). A handle is a 64-bit logical identity:
 *
 *     handle = (generation << 32) | slot_index
 *
 *   - Upper 32 bits: slot-lifetime generation (nonzero for a valid handle).
 *   - Lower 32 bits: 1-based slot index.
 *   - 0 (S_OM_REF_NONE) is the invalid handle.
 *
 * Object identity is therefore a (slot, lifetime) pair, not merely a storage
 * location. A stale handle cannot alias a later occupant of the same slot:
 * validation requires the slot to be live and its CURRENT generation to equal
 * the handle's generation. Releasing an object invalidates its handle
 * immediately (the slot is no longer live); reusing the slot advances the
 * generation, so an old handle also fails after reuse. See s_om_slot for how
 * the current generation is stored.
 */
typedef s_u64 s_om_ref;
#define S_OM_REF_NONE 0ULL
#define S_OM_MAX_GEN 0xFFFFFFFFu

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
 * allocation, free, payload writes, and the GC's own root add/remove all fail
 * with S_OM_ECOLLECT. This makes incremental marking safe by precluding graph
 * mutation during collection.
 */
s_om_err s_om_set_collection_lock(s_om_heap h, bool on);
bool s_om_is_collecting(s_om_heap h);

/*
 * Allocate an object of `kind` with `count` units and `payload_bytes` bytes
 * of string payload (only meaningful for S_OBJ_STR). On success `*out_obj`
 * receives the object handle (never S_OM_REF_NONE).
 *
 * For STR, payload_bytes is the byte length and count == payload_bytes. For
 * array/record/closure, count is the number of child s_value slots and
 * payload_bytes is ignored (derived). Exception carries 3 child slots.
 *
 * The object is zero-filled, colour is S_GC_WHITE, its slot magic is set and
 * its slot generation is committed. Failure (region exhausted, generation
 * exhaustion, fault injection, overflow) leaves existing objects intact and
 * returns an error WITHOUT publishing any handle.
 */
s_om_err s_om_alloc(s_om_heap h, s_obj_kind kind, s_u32 count,
                    s_size payload_bytes, s_om_ref *out_obj);

/* Free an object, returning its slot to the segregated freelist for its size
 * class. The handle is invalidated immediately (stale thereafter). Double-free/
 * stale handles fail with S_OM_ENOENT. In debug builds the payload is poisoned. */
s_om_err s_om_free(s_om_heap h, s_om_ref obj);

/*
 * Validate a managed handle: OK iff the heap is live, the slot index is in
 * range, the slot is allocated, its stored generation matches the handle's
 * generation, and the metadata checks pass. Any other result is rejected.
 */
s_om_err s_om_validate(s_om_heap h, s_om_ref obj);

/*
 * Cheap identity liveness: true iff the handle's slot is live and its stored
 * generation matches (no header-integrity check). This is the check the GC uses
 * for traversal/root seeding so that header corruption on a still-identified
 * object cannot cause it to be reclaimed while still referenced.
 */
bool s_om_is_live(s_om_heap h, s_om_ref obj);

/*
 * Trusted-runtime only: resolve a live object handle to a stable pointer to its
 * header start within its region, plus its rounded size. The pointer is stable
 * because objects are non-moving and collection (stop-the-world) never coexists
 * with runtime access. Ordinary SURU programs never obtain this; it exists so
 * the VM can access payloads directly (the architecture's "runtime layer sees
 * addresses"). Fails (ENOENT) for stale/invalid handles.
 */
s_om_err s_om_inspect(s_om_heap h, s_om_ref obj, const void **base_out,
                       s_size *rounded_out);

/*
 * Iterate live objects. Pass a cursor (u32 slot index) that starts at 0. On
 * success the cursor and *out_obj are set to the next live object's handle and
 * S_OM_OK is returned. When no further live object exists, S_OM_ENOENT is
 * returned. The iteration is a snapshot-free strictly-forward scan; freeing the
 * just-returned object during iteration is permitted (later slots are
 * unaffected). `out_obj` receives a full managed handle (slot + generation).
 */
s_om_err s_om_next_live(s_om_heap h, s_u32 *cursor, s_om_ref *out_obj);

/* Introspection (all take full managed handles). */
s_om_err s_om_kind(s_om_heap h, s_om_ref obj, s_obj_kind *out);
s_om_err s_om_count(s_om_heap h, s_om_ref obj, s_u32 *out);
s_om_err s_om_size(s_om_heap h, s_om_ref obj, s_size *out);
s_om_err s_om_colour(s_om_heap h, s_om_ref obj, s_u8 *out);
s_om_err s_om_set_colour(s_om_heap h, s_om_ref obj, s_u8 colour);

/* Payload access. Offsets are relative to the object payload (byte 0 is the
 * first payload byte after the header). Bounds are checked against the rounded
 * object size; out-of-range access fails with S_OM_EOOB. Writes are forbidden
 * (S_OM_ECOLLECT) while a collection is active. */
s_om_err s_om_read(s_om_heap h, s_om_ref obj, s_size off, void *dst, s_size len);
s_om_err s_om_write(s_om_heap h, s_om_ref obj, s_size off,
                    const void *src, s_size len);
s_om_err s_om_read_u8(s_om_heap h, s_om_ref obj, s_size off, s_u8 *out);
s_om_err s_om_read_u32(s_om_heap h, s_om_ref obj, s_size off, s_u32 *out);
s_om_err s_om_read_u64(s_om_heap h, s_om_ref obj, s_size off, s_u64 *out);
s_om_err s_om_write_u8(s_om_heap h, s_om_ref obj, s_size off, s_u8 v);
s_om_err s_om_write_u32(s_om_heap h, s_om_ref obj, s_size off, s_u32 v);
s_om_err s_om_write_u64(s_om_heap h, s_om_ref obj, s_size off, s_u64 v);

/* Stats + fault injection (mirrors platform_mem conventions). */
s_u64 s_om_live_bytes(s_om_heap h);
s_u32 s_om_live_objects(s_om_heap h);
s_u32 s_om_active_count(void);
void s_om_dispose_all(void);
void s_om_set_fail_alloc(bool enabled);
void s_om_set_alloc_fail_after(s_u32 n_ok);

/*
 * Test-only helpers (C side only; never exposed through the SURU language).
 * They are used to exercise generation exhaustion and to assert handle internals
 * without billions of allocations.
 */
s_u32 s_om_test_slot_index(s_om_ref obj);
s_u32 s_om_test_generation(s_om_ref obj);
s_om_ref s_om_test_make_handle(s_u32 slot_index, s_u32 generation);
/* Set a FREE slot's generation. Fails (S_OM_EINVAL) if the slot is live. */
s_om_err s_om_test_set_generation(s_om_heap h, s_u32 slot_index, s_u32 generation);
/* Test-only: overwrite an inline object-header byte/u32 (offsets 0..15:
 * size@0, count@4, magic@8, kind@12, colour@13, flags@14) of a live object, to
 * exercise metadata-corruption detection. Fails if the handle is invalid or the
 * offset is out of range. */
s_om_err s_om_test_set_hdr_u8(s_om_heap h, s_om_ref obj, s_size off, s_u8 v);
s_om_err s_om_test_set_hdr_u32(s_om_heap h, s_om_ref obj, s_size off, s_u32 v);

/* Limits. */
#define S_OM_MAX_HEAPS 32u
#define S_OM_MAX_OBJECTS 65536u
#define S_OM_ALIGN 16u
#define S_OM_HEADER_SIZE 16u
#define S_OM_SMALL_MAX 4096u
#define S_OM_SMALL_BUCKETS (S_OM_SMALL_MAX / S_OM_ALIGN + 1u)

#ifdef __cplusplus
}
#endif

#endif /* SURU_OBJMODEL_H */
