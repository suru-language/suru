/* SURU trusted platform memory interface (Stage 1-2).
 *
 * This is the bootstrap-only platform adapter described in
 * docs/memory-architecture.md §5. Its whole purpose is to acquire and
 * release raw, aligned memory regions behind an opaque, hand-validated
 * interface. No raw pointer ever leaves this module; callers operate on an
 * opaque handle and checked offsets.
 *
 * Scope: TRUSTED runtime code only. Ordinary SURU programs cannot obtain a
 * RawBlock handle or reach into host memory through this interface.
 *
 * The managed GC heap, object model, and collector are intentionally separate
 * and are NOT implemented here.
 */

#ifndef SURU_PLATFORM_MEM_H
#define SURU_PLATFORM_MEM_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque RawBlock handle. 0 is always invalid. */
typedef s_u32 s_pm_handle;

/* Result codes. A zero result means success. */
typedef enum s_pm_err {
    S_PM_OK = 0,
    S_PM_EINVAL,     /* invalid argument (e.g. non-power-of-two alignment) */
    S_PM_ENOENT,     /* invalid, stale, released, or double-released handle */
    S_PM_EOOB,       /* access outside the region bounds */
    S_PM_EOVERFLOW,  /* requested range overflows the region or size_t */
    S_PM_ENOMEM,     /* allocation failed */
    S_PM_ELIMIT,     /* no free region slot (registry exhausted) */
} s_pm_err;

/*
 * Acquire a raw memory region of `size` bytes aligned to `align` bytes.
 *
 *   size  : number of usable bytes. 0 is permitted and yields a valid,
 *           zero-length region (reads/writes of length > 0 fail with
 *           S_PM_EOOB; length-0 accesses succeed).
 *   align : required alignment, power of two > 0. Passing 0 selects the
 *           default alignment (16 bytes).
 *   out   : receives the opaque handle on success. On failure it is set to 0.
 *
 * Pre : align is 0 or a power of two; size is finite; `out` points to a
 *       writable s_pm_handle.
 * Post: on success the region is acquired, owned by the caller, and its
 *       base address satisfies the alignment. On failure no region is
 *       acquired and `out` is 0.
 * Error: S_PM_EINVAL (bad alignment), S_PM_EOVERFLOW (size + align - 1
 *       overflows), S_PM_ENOMEM (allocation failed), S_PM_ELIMIT (registry
 *       full).
 * This operation never triggers garbage collection.
 */
s_pm_err s_pm_acquire(s_size size, s_size align, s_pm_handle *out);

/*
 * Release a region previously returned by s_pm_acquire.
 *
 * Pre : handle is valid and still owned by the caller.
 * Post: the region is released and the handle becomes stale; a second
 *       release of the same handle fails with S_PM_ENOENT.
 * Error: S_PM_ENOENT (invalid, already released, or double release).
 */
s_pm_err s_pm_release(s_pm_handle h);

/*
 * Query region properties.
 *
 * Pre : handle is valid.
 * Post: *out receives the usable size / alignment.
 * Error: S_PM_ENOENT (invalid/stale handle).
 */
s_pm_err s_pm_size(s_pm_handle h, s_size *out);
s_pm_err s_pm_align(s_pm_handle h, s_size *out);

/*
 * Validate a handle without performing an access.
 * Returns S_PM_OK if the handle is valid and owned; S_PM_ENOENT otherwise.
 */
s_pm_err s_pm_validate(s_pm_handle h);

/*
 * Read/write `len` bytes at byte offset `off` within the region.
 *
 * Pre : handle valid; `dst`/`src` valid for `len` bytes; 0 <= off <= size;
 *       0 <= len <= size - off.
 * Post: bytes are copied; otherwise nothing is modified.
 * Error: S_PM_ENOENT (invalid/stale handle), S_PM_EOOB (off > size or
 *       len > size - off), S_PM_EOVERFLOW (range would overflow size_t).
 *       A length of 0 is a no-op and always succeeds.
 */
s_pm_err s_pm_read(s_pm_handle h, s_size off, void *dst, s_size len);
s_pm_err s_pm_write(s_pm_handle h, s_size off, const void *src, s_size len);

/* Typed, bounds-checked single-value accessors. */
s_pm_err s_pm_read_u8(s_pm_handle h, s_size off, s_u8 *out);
s_pm_err s_pm_read_u32(s_pm_handle h, s_size off, s_u32 *out);
s_pm_err s_pm_read_u64(s_pm_handle h, s_size off, s_u64 *out);
s_pm_err s_pm_write_u8(s_pm_handle h, s_size off, s_u8 v);
s_pm_err s_pm_write_u32(s_pm_handle h, s_size off, s_u32 v);
s_pm_err s_pm_write_u64(s_pm_handle h, s_size off, s_u64 v);

/* Fault injection for tests. */
void s_pm_set_fail_alloc(bool enabled);
void s_pm_set_alloc_fail_after(s_u32 n_ok);

/* Introspection helpers for tests/tooling. */
s_u32 s_pm_active_count(void);
void s_pm_dispose_all(void);

/*
 * Trusted-runtime only: return the aligned usable base pointer of a live region.
 * This is the single, deliberate address-escape for the trusted runtime (the
 * object model and VM, which per the architecture "see addresses"). Ordinary
 * SURU programs can never reach this interface. The pointer is stable for the
 * region's lifetime (no movement).
 */
s_pm_err s_pm_ptr(s_pm_handle h, void **out);

#ifdef __cplusplus
}
#endif

#endif /* SURU_PLATFORM_MEM_H */
