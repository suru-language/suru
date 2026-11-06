#include "suru/vmobj.h"

s_om_err s_vmobj_alloc(s_om_heap h, s_obj_kind kind, s_u32 count,
                       s_size payload_bytes, s_om_ref *out) {
    return s_om_alloc(h, kind, count, payload_bytes, out);
}

s_om_err s_vmobj_validate(s_om_heap h, s_om_ref o) {
    return s_om_validate(h, o);
}

bool s_vmobj_is_live(s_om_heap h, s_om_ref o) {
    return s_om_is_live(h, o);
}

s_om_err s_vmobj_inspect(s_om_heap h, s_om_ref o, const void **base,
                         s_size *rounded) {
    return s_om_inspect(h, o, base, rounded);
}

s_om_err s_vmobj_read(s_om_heap h, s_om_ref o, s_size off, void *dst, s_size len) {
    return s_om_read(h, o, off, dst, len);
}

s_om_err s_vmobj_write(s_om_heap h, s_om_ref o, s_size off,
                       const void *src, s_size len) {
    return s_om_write(h, o, off, src, len);
}

s_om_err s_vmobj_read_u8(s_om_heap h, s_om_ref o, s_size off, s_u8 *out) {
    return s_om_read_u8(h, o, off, out);
}

s_om_err s_vmobj_read_u32(s_om_heap h, s_om_ref o, s_size off, s_u32 *out) {
    return s_om_read_u32(h, o, off, out);
}

s_om_err s_vmobj_read_u64(s_om_heap h, s_om_ref o, s_size off, s_u64 *out) {
    return s_om_read_u64(h, o, off, out);
}

s_om_err s_vmobj_write_u8(s_om_heap h, s_om_ref o, s_size off, s_u8 v) {
    return s_om_write_u8(h, o, off, v);
}

s_om_err s_vmobj_write_u32(s_om_heap h, s_om_ref o, s_size off, s_u32 v) {
    return s_om_write_u32(h, o, off, v);
}

s_om_err s_vmobj_write_u64(s_om_heap h, s_om_ref o, s_size off, s_u64 v) {
    return s_om_write_u64(h, o, off, v);
}

s_om_err s_vmobj_temp_root(s_om_gc *g, s_om_ref o) {
    return s_om_gc_add_root(g, o);
}

s_om_err s_vmobj_temp_unroot(s_om_gc *g, s_om_ref o) {
    return s_om_gc_remove_root(g, o);
}

s_u32 s_vmobj_collect(s_om_gc *g) {
    return s_om_gc_collect(g);
}
