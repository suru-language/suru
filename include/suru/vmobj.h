#ifndef SURU_VMOBJ_H
#define SURU_VMOBJ_H

#include "suru/base.h"
#include "suru/value.h"
#include "suru/objmodel.h"
#include "suru/objgc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Narrow VM object-access interface (Stage 11D-1).                    */
/*                                                                     */
/* This is the only surface the VM uses to talk to the object model/   */
/* collector. It is trusted-runtime-only: ordinary SURU programs can    */
/* never reach it. It deliberately exposes no allocator internals and   */
/* no raw addresses except `s_vmobj_inspect` (the sole trusted address  */
/* escape, mirroring `s_om_inspect`). All handles are full 64-bit       */
/* generation-protected values; they are never truncated.              */
/* ------------------------------------------------------------------ */

/* Allocate an object of `kind`. On success `*out` receives a handle
 * (never S_OM_REF_NONE); on failure no handle is published. */
s_om_err s_vmobj_alloc(s_om_heap h, s_obj_kind kind, s_u32 count,
                       s_size payload_bytes, s_om_ref *out);

/* Stage 11D-B1: allocate through the object model and construct an
 * S_VAL_OBJ value carrying the complete generation-protected handle. On success
 * `*out` is a genuine handle value; on failure `*out` is left unchanged and the
 * error is returned. The allocated object is owned by the new object model
 * (destroyed via s_om_free); a legacy pointer is never manufactured as a handle. */
s_om_err s_vmobj_alloc_value(s_om_heap h, s_obj_kind kind, s_u32 count,
                             s_size payload_bytes, s_value *out);

/* Full validation and cheap identity liveness (slot+generation, no
 * header-intgrity check), respectively. */
s_om_err s_vmobj_validate(s_om_heap h, s_om_ref o);
bool s_vmobj_is_live(s_om_heap h, s_om_ref o);

/* Family/object introspection over handles (validated). */
s_om_err s_vmobj_kind(s_om_heap h, s_om_ref o, s_obj_kind *out);
s_om_err s_vmobj_count(s_om_heap h, s_om_ref o, s_u32 *out);
s_om_err s_vmobj_size(s_om_heap h, s_om_ref o, s_size *out);

/* Resolve a live handle to a stable pointer to its header start plus its
 * rounded size. Trusted-runtime only; never exposed to ordinary programs. */
s_om_err s_vmobj_inspect(s_om_heap h, s_om_ref o, const void **base,
                         s_size *rounded);

/* Bounds-checked payload access. Writes fail with S_OM_ECOLLECT while a
 * collection is active. */
s_om_err s_vmobj_read(s_om_heap h, s_om_ref o, s_size off, void *dst, s_size len);
s_om_err s_vmobj_write(s_om_heap h, s_om_ref o, s_size off,
                       const void *src, s_size len);
s_om_err s_vmobj_read_u8(s_om_heap h, s_om_ref o, s_size off, s_u8 *out);
s_om_err s_vmobj_read_u32(s_om_heap h, s_om_ref o, s_size off, s_u32 *out);
s_om_err s_vmobj_read_u64(s_om_heap h, s_om_ref o, s_size off, s_u64 *out);
s_om_err s_vmobj_write_u8(s_om_heap h, s_om_ref o, s_size off, s_u8 v);
s_om_err s_vmobj_write_u32(s_om_heap h, s_om_ref o, s_size off, s_u32 v);
s_om_err s_vmobj_write_u64(s_om_heap h, s_om_ref o, s_size off, s_u64 v);

/* Scoped C-local temporary roots. `o` is added to the collector's root set
 * and must be removed on every path (normal, early return, error) before the
 * next collection. Refused while marking is active. */
s_om_err s_vmobj_temp_root(s_om_gc *g, s_om_ref o);
s_om_err s_vmobj_temp_unroot(s_om_gc *g, s_om_ref o);

/* Run a full collection through a bound collector. Returns reclaimed count. */
s_u32 s_vmobj_collect(s_om_gc *g);

#ifdef __cplusplus
}
#endif

#endif /* SURU_VMOBJ_H */
