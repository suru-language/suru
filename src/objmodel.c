#include "suru/objmodel.h"
#include "suru/mem.h"
#include "suru/platform_mem.h"
#include "suru/value.h"

#include <string.h>

#define S_OM_SLOT_MAGIC 0x0B3A10EDu
#define S_OM_POISON 0xCDu

/* ------------------------------------------------------------------ */
/* Slot registry (bookkeeping, lives in a RawBlock region)             */
/* ------------------------------------------------------------------ */

typedef struct s_om_slot {
    s_u32 magic;         /* S_OM_SLOT_MAGIC when live; 0 when free */
    s_u32 off;           /* byte offset of the object in the object region */
    s_u32 size_rounded;  /* total rounded bytes (header + payload) */
    s_u32 count;         /* str byte length or child slot count */
    s_u8 kind;           /* s_obj_kind */
    s_u8 colour;         /* tri-color mark state */
    s_u8 flags;
    s_u8 _pad;
    s_u32 next_free;     /* freelist link (slot index) when free */
    s_u32 gen;           /* current slot-lifetime generation (handles carry it) */
} s_om_slot;

typedef struct s_om_heap_ {
    s_pm_handle slots_blk;
    s_pm_handle obj_blk;
    s_u32 nslots;        /* slot capacity */
    s_u32 obj_bytes;     /* usable object-region bytes */
    s_u32 obj_top;       /* bump cursor (next free byte offset) */
    s_u32 next_slot;     /* next fresh slot index (1-based) */
    s_u32 small_head[S_OM_SMALL_BUCKETS];
    s_u32 large_head;
    s_u32 live_objects;
    s_u64 live_bytes;
    bool in_use;
    bool collecting;      /* stop-the-world collection lock */
} s_om_heap_;

static s_om_heap_ heaps[S_OM_MAX_HEAPS];
static bool fail_alloc = false;
static s_u32 alloc_fail_after = 0;
static s_u32 alloc_count = 0;

static s_om_heap_ *heap_of(s_om_heap h) {
    if (h == 0) return NULL;
    s_u32 idx = h - 1;
    if (idx >= S_OM_MAX_HEAPS) return NULL;
    s_om_heap_ *hp = &heaps[idx];
    if (!hp->in_use) return NULL;
    return hp;
}

static s_om_err pm_err_map(s_pm_err pe) {
    switch (pe) {
    case S_PM_OK: return S_OM_OK;
    case S_PM_EINVAL: return S_OM_EINVAL;
    case S_PM_ENOENT: return S_OM_ENOENT;
    case S_PM_EOOB: return S_OM_EOOB;
    case S_PM_EOVERFLOW: return S_OM_EOVERFLOW;
    case S_PM_ENOMEM: return S_OM_ENOMEM;
    case S_PM_ELIMIT: return S_OM_ENOMEM;
    }
    return S_OM_EINVAL;
}

/* ------------------------------------------------------------------ */
/* Handle <-> slot/generation                                          */
/* ------------------------------------------------------------------ */

static inline s_u32 ref_slot(s_om_ref r) { return (s_u32)(r & 0xFFFFFFFFu); }
static inline s_u32 ref_gen(s_om_ref r) { return (s_u32)(r >> 32); }
static inline s_om_ref make_ref(s_u32 idx, s_u32 gen) {
    return ((s_om_ref)gen << 32) | (s_om_ref)idx;
}

/* ------------------------------------------------------------------ */
/* Slot registry access                                                */
/* ------------------------------------------------------------------ */

static s_om_err slot_load(s_om_heap_ *hp, s_u32 idx, s_om_slot *out) {
    if (idx == 0 || idx > hp->nslots) return S_OM_ENOENT;
    s_size b = (s_size)(idx - 1) * (s_size)sizeof(s_om_slot);
    return pm_err_map(s_pm_read(hp->slots_blk, b, out, sizeof(*out)));
}

static s_om_err slot_store(s_om_heap_ *hp, s_u32 idx, const s_om_slot *v) {
    if (idx == 0 || idx > hp->nslots) return S_OM_ENOENT;
    s_size b = (s_size)(idx - 1) * (s_size)sizeof(s_om_slot);
    return pm_err_map(s_pm_write(hp->slots_blk, b, v, sizeof(*v)));
}

static s_om_err slot_live_index(s_om_heap_ *hp, s_u32 idx, s_om_slot *out) {
    s_om_err e = slot_load(hp, idx, out);
    if (e != S_OM_OK) return e;
    if (out->magic != S_OM_SLOT_MAGIC) return S_OM_ENOENT;
    return S_OM_OK;
}

static s_om_err ref_check(s_om_heap_ *hp, s_om_ref r, s_om_slot *out) {
    if (r == S_OM_REF_NONE) return S_OM_ENOENT;
    s_u32 idx = ref_slot(r);
    s_u32 gen = ref_gen(r);
    if (idx == 0 || idx > hp->nslots) return S_OM_ENOENT;
    s_om_err e = slot_live_index(hp, idx, out);
    if (e != S_OM_OK) return e;
    if (out->gen != gen) return S_OM_ENOENT;
    return S_OM_OK;
}

/* ------------------------------------------------------------------ */
/* Payload access (checked)                                            */
/* ------------------------------------------------------------------ */

static s_om_err om_payload_access(s_om_heap_ *hp, s_om_ref obj, s_size off,
                                  void *buf, s_size len, bool wr) {
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    if (wr && hp->collecting) return S_OM_ECOLLECT;
    s_size pay = (s_size)sl.size_rounded - S_OM_HEADER_SIZE;
    if (off > pay) return S_OM_EOOB;
    if (len > pay - off) return S_OM_EOOB;
    s_size abs = (s_size)sl.off + S_OM_HEADER_SIZE + off;
    s_pm_err pe = wr ? s_pm_write(hp->obj_blk, abs, buf, len)
                     : s_pm_read(hp->obj_blk, abs, buf, len);
    return pm_err_map(pe);
}

static s_om_err hdr_write_u32(s_om_heap_ *hp, s_u32 off, s_size pos, s_u32 v) {
    return pm_err_map(s_pm_write_u32(hp->obj_blk, off + pos, v));
}

static s_om_err hdr_write_u8(s_om_heap_ *hp, s_u32 off, s_size pos, s_u8 v) {
    return pm_err_map(s_pm_write_u8(hp->obj_blk, off + pos, v));
}

static s_om_err obj_zero(s_om_heap_ *hp, s_u32 abs_off, s_size len) {
    static const unsigned char zeros[64] = {0};
    s_size done = 0;
    while (done < len) {
        s_size n = len - done;
        if (n > (s_size)sizeof(zeros)) n = (s_size)sizeof(zeros);
        s_pm_err pe = s_pm_write(hp->obj_blk, (s_size)abs_off + done, zeros, n);
        if (pe != S_PM_OK) return pm_err_map(pe);
        done += n;
    }
    return S_OM_OK;
}

#ifndef NDEBUG
static void obj_poison(s_om_heap_ *hp, s_u32 abs_off, s_size len) {
    unsigned char buf[64];
    memset(buf, S_OM_POISON, sizeof(buf));
    s_size done = 0;
    while (done < len) {
        s_size n = len - done;
        if (n > (s_size)sizeof(buf)) n = (s_size)sizeof(buf);
        (void)s_pm_write(hp->obj_blk, (s_size)abs_off + done, buf, n);
        done += n;
    }
}
#endif

/* ------------------------------------------------------------------ */
/* Heap lifecycle                                                      */
/* ------------------------------------------------------------------ */

s_om_err s_om_heap_create(s_u32 max_objects, s_size obj_bytes, s_om_heap *out) {
    if (!out) return S_OM_EINVAL;
    *out = 0;
    if (max_objects == 0 || max_objects > S_OM_MAX_OBJECTS) return S_OM_EINVAL;
    if (obj_bytes < S_OM_HEADER_SIZE) return S_OM_EINVAL;
    if (obj_bytes > (s_size)UINT32_MAX) return S_OM_EOVERFLOW;

    s_size slots_bytes;
    if (!s_size_mul_ovf((s_size)max_objects, (s_size)sizeof(s_om_slot),
                        &slots_bytes)) {
        return S_OM_EOVERFLOW;
    }
    if (slots_bytes > (s_size)UINT32_MAX) return S_OM_EOVERFLOW;

    s_u32 idx;
    bool found = false;
    for (idx = 0; idx < S_OM_MAX_HEAPS; ++idx) {
        if (!heaps[idx].in_use) { found = true; break; }
    }
    if (!found) return S_OM_ENOMEM;

    s_pm_handle sb = 0, ob = 0;
    s_pm_err pe = s_pm_acquire(slots_bytes, S_OM_ALIGN, &sb);
    if (pe != S_PM_OK) return pm_err_map(pe);
    pe = s_pm_acquire(obj_bytes, S_OM_ALIGN, &ob);
    if (pe != S_PM_OK) {
        (void)s_pm_release(sb);
        return pm_err_map(pe);
    }

    s_om_heap_ *hp = &heaps[idx];
    memset(hp, 0, sizeof(*hp));
    hp->slots_blk = sb;
    hp->obj_blk = ob;
    hp->nslots = max_objects;
    hp->obj_bytes = (s_u32)obj_bytes;
    hp->obj_top = 0;
    hp->next_slot = 1;
    hp->large_head = 0;
    hp->in_use = true;

    *out = (s_om_heap)(idx + 1);
    return S_OM_OK;
}

s_om_err s_om_heap_destroy(s_om_heap h) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    (void)s_pm_release(hp->slots_blk);
    (void)s_pm_release(hp->obj_blk);
    memset(hp, 0, sizeof(*hp));
    return S_OM_OK;
}

s_om_err s_om_heap_validate(s_om_heap h) {
    return heap_of(h) ? S_OM_OK : S_OM_ENOENT;
}

/* ------------------------------------------------------------------ */
/* Allocation                                                          */
/* ------------------------------------------------------------------ */

s_om_err s_om_alloc(s_om_heap h, s_obj_kind kind, s_u32 count,
                    s_size payload_bytes, s_om_ref *out_obj) {
    if (!out_obj) return S_OM_EINVAL;
    *out_obj = S_OM_REF_NONE;
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    if (hp->collecting) return S_OM_ECOLLECT;
    if (kind < S_OBJ_STR || kind > S_OBJ_EXCEPTION) return S_OM_EINVAL;
    if (fail_alloc || (alloc_fail_after > 0 && alloc_count >= alloc_fail_after)) {
        return S_OM_ENOMEM;
    }

    s_size payload;
    if (kind == S_OBJ_STR) {
        payload = payload_bytes;
    } else {
        s_size cs;
        if (!s_size_mul_ovf((s_size)count, (s_size)sizeof(s_value), &cs)) {
            return S_OM_EOVERFLOW;
        }
        payload = cs;
    }

    s_size total;
    if (!s_size_add_ovf(S_OM_HEADER_SIZE, payload, &total)) return S_OM_EOVERFLOW;
    s_size rounded = (total + (S_OM_ALIGN - 1)) & ~(s_size)(S_OM_ALIGN - 1);
    if (rounded < S_OM_HEADER_SIZE) rounded = S_OM_HEADER_SIZE;
    if (rounded > (s_size)UINT32_MAX) return S_OM_EOVERFLOW;
    s_u32 r32 = (s_u32)rounded;

    s_u32 *head = (r32 <= S_OM_SMALL_MAX)
        ? &hp->small_head[r32 / S_OM_ALIGN]
        : &hp->large_head;

    s_u32 obj = *head;
    s_u32 off;
    s_u32 gen = 1;
    bool reused = false;
    s_om_slot sl;

    /* Reuse path: walk the freelist, skipping retired (max-generation) slots. */
    while (obj != 0) {
        s_om_err e = slot_load(hp, obj, &sl);
        if (e != S_OM_OK) return e;
        if (sl.magic != 0) return S_OM_EINVAL; /* corrupt/duplicate list entry */
        if (sl.gen < S_OM_MAX_GEN) { reused = true; break; }
        obj = sl.next_free; /* retired: drop and continue searching */
    }
    if (reused) {
        *head = sl.next_free;
        off = sl.off;
        gen = sl.gen + 1;
    } else {
        /* Bump a fresh slot. */
        if (hp->next_slot > hp->nslots) return S_OM_ENOMEM;
        if ((s_size)hp->obj_top + r32 > (s_size)hp->obj_bytes) return S_OM_ENOMEM;
        off = hp->obj_top;
        hp->obj_top += r32;
        obj = hp->next_slot++;
        gen = 1;
    }

    s_om_slot sl2;
    memset(&sl2, 0, sizeof(sl2));
    sl2.magic = S_OM_SLOT_MAGIC;
    sl2.off = off;
    sl2.size_rounded = r32;
    sl2.count = count;
    sl2.kind = (s_u8)kind;
    sl2.colour = S_GC_WHITE;
    sl2.flags = 0;
    sl2.next_free = 0;
    sl2.gen = gen;

    s_om_err e = slot_store(hp, obj, &sl2);
    if (e != S_OM_OK) return e;
    e = hdr_write_u32(hp, off, 0, r32);
    if (e != S_OM_OK) return e;
    e = hdr_write_u32(hp, off, 4, count);
    if (e != S_OM_OK) return e;
    e = hdr_write_u32(hp, off, 8, S_OBJ_MAGIC);
    if (e != S_OM_OK) return e;
    e = hdr_write_u8(hp, off, 12, (s_u8)kind);
    if (e != S_OM_OK) return e;
    e = hdr_write_u8(hp, off, 13, S_GC_WHITE);
    if (e != S_OM_OK) return e;
    e = hdr_write_u8(hp, off, 14, 0);
    if (e != S_OM_OK) return e;
    e = hdr_write_u8(hp, off, 15, 0);
    if (e != S_OM_OK) return e;
    e = obj_zero(hp, off + S_OM_HEADER_SIZE, (s_size)r32 - S_OM_HEADER_SIZE);
    if (e != S_OM_OK) return e;

    hp->live_objects++;
    hp->live_bytes += r32;
    alloc_count++;

    *out_obj = make_ref(obj, gen);
    return S_OM_OK;
}

s_om_err s_om_free(s_om_heap h, s_om_ref obj) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    if (hp->collecting) return S_OM_ECOLLECT;
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    s_u32 idx = ref_slot(obj);

    s_u32 *head = (sl.size_rounded <= S_OM_SMALL_MAX)
        ? &hp->small_head[sl.size_rounded / S_OM_ALIGN]
        : &hp->large_head;
    sl.magic = 0;
    sl.next_free = *head;
    e = slot_store(hp, idx, &sl);
    if (e != S_OM_OK) return e;
    *head = idx;

#ifndef NDEBUG
    obj_poison(hp, sl.off + S_OM_HEADER_SIZE,
               (s_size)sl.size_rounded - S_OM_HEADER_SIZE);
#endif

    hp->live_objects--;
    hp->live_bytes -= sl.size_rounded;
    return S_OM_OK;
}

static s_om_err slot_check_metadata(s_om_heap_ *hp, const s_om_slot *sl) {
    if (sl->size_rounded < S_OM_HEADER_SIZE) return S_OM_EINVAL;
    if ((sl->size_rounded & (S_OM_ALIGN - 1u)) != 0) return S_OM_EINVAL;
    if ((sl->off & (S_OM_ALIGN - 1u)) != 0) return S_OM_EINVAL;
    if ((s_size)sl->off + sl->size_rounded > (s_size)hp->obj_bytes) {
        return S_OM_EINVAL;
    }
    if (sl->kind > S_OBJ_EXCEPTION) return S_OM_EINVAL;
    if (sl->colour > S_GC_BLACK) return S_OM_EINVAL;
    if (sl->flags != 0) return S_OM_EINVAL;
    s_u32 hsize = 0, hmagic = 0;
    s_u8 hkind = 0, hcol = 0, hfl = 0;
    if (s_pm_read_u32(hp->obj_blk, sl->off + 0, &hsize) != S_PM_OK) {
        return S_OM_EINVAL;
    }
    if (s_pm_read_u32(hp->obj_blk, sl->off + 8, &hmagic) != S_PM_OK) {
        return S_OM_EINVAL;
    }
    if (s_pm_read_u8(hp->obj_blk, sl->off + 12, &hkind) != S_PM_OK) {
        return S_OM_EINVAL;
    }
    if (s_pm_read_u8(hp->obj_blk, sl->off + 13, &hcol) != S_PM_OK) {
        return S_OM_EINVAL;
    }
    if (s_pm_read_u8(hp->obj_blk, sl->off + 14, &hfl) != S_PM_OK) {
        return S_OM_EINVAL;
    }
    if (hmagic != S_OBJ_MAGIC) return S_OM_EINVAL;
    if (hsize != sl->size_rounded) return S_OM_EINVAL;
    if (hkind != sl->kind) return S_OM_EINVAL;
    if (hcol != sl->colour) return S_OM_EINVAL;
    if (hfl != sl->flags) return S_OM_EINVAL;
    return S_OM_OK;
}

s_om_err s_om_validate(s_om_heap h, s_om_ref obj) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    return slot_check_metadata(hp, &sl);
}

bool s_om_is_live(s_om_heap h, s_om_ref obj) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return false;
    s_om_slot sl;
    return ref_check(hp, obj, &sl) == S_OM_OK;
}

s_om_err s_om_inspect(s_om_heap h, s_om_ref obj, const void **base_out,
                       s_size *rounded_out) {
    if (!base_out || !rounded_out) return S_OM_EINVAL;
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    void *base = NULL;
    if (s_pm_ptr(hp->obj_blk, &base) != S_PM_OK) return S_OM_ENOENT;
    *base_out = (const unsigned char *)base + sl.off;
    *rounded_out = (s_size)sl.size_rounded;
    return S_OM_OK;
}

s_om_err s_om_next_live(s_om_heap h, s_u32 *cursor, s_om_ref *out_obj) {
    if (!cursor || !out_obj) return S_OM_EINVAL;
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    s_u32 i = *cursor;
    while (i < hp->nslots) {
        i++;
        s_om_slot sl;
        if (i <= hp->nslots &&
            slot_load(hp, i, &sl) == S_OM_OK &&
            sl.magic == S_OM_SLOT_MAGIC) {
            *cursor = i;
            *out_obj = make_ref(i, sl.gen);
            return S_OM_OK;
        }
    }
    return S_OM_ENOENT;
}

/* ------------------------------------------------------------------ */
/* Introspection                                                       */
/* ------------------------------------------------------------------ */

s_om_err s_om_kind(s_om_heap h, s_om_ref obj, s_obj_kind *out) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    if (out) *out = (s_obj_kind)sl.kind;
    return S_OM_OK;
}

s_om_err s_om_count(s_om_heap h, s_om_ref obj, s_u32 *out) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    if (out) *out = sl.count;
    return S_OM_OK;
}

s_om_err s_om_size(s_om_heap h, s_om_ref obj, s_size *out) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    if (out) *out = (s_size)sl.size_rounded;
    return S_OM_OK;
}

s_om_err s_om_colour(s_om_heap h, s_om_ref obj, s_u8 *out) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    if (out) *out = sl.colour;
    return S_OM_OK;
}

s_om_err s_om_set_colour(s_om_heap h, s_om_ref obj, s_u8 colour) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    if (colour > S_GC_BLACK) return S_OM_EINVAL;
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    sl.colour = colour;
    s_om_err se = slot_store(hp, ref_slot(obj), &sl);
    if (se != S_OM_OK) return se;
    return hdr_write_u8(hp, sl.off, 13, colour);
}

/* ------------------------------------------------------------------ */
/* Payload accessors                                                   */
/* ------------------------------------------------------------------ */

s_om_err s_om_read(s_om_heap h, s_om_ref obj, s_size off, void *dst, s_size len) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    if (!dst) return S_OM_EINVAL;
    if (len == 0) return S_OM_OK;
    return om_payload_access(hp, obj, off, dst, len, false);
}

s_om_err s_om_write(s_om_heap h, s_om_ref obj, s_size off,
                    const void *src, s_size len) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    if (!src) return S_OM_EINVAL;
    if (len == 0) return S_OM_OK;
    return om_payload_access(hp, obj, off, (void *)src, len, true);
}

s_om_err s_om_read_u8(s_om_heap h, s_om_ref obj, s_size off, s_u8 *out) {
    return s_om_read(h, obj, off, out, 1);
}

s_om_err s_om_read_u32(s_om_heap h, s_om_ref obj, s_size off, s_u32 *out) {
    return s_om_read(h, obj, off, out, 4);
}

s_om_err s_om_read_u64(s_om_heap h, s_om_ref obj, s_size off, s_u64 *out) {
    return s_om_read(h, obj, off, out, 8);
}

s_om_err s_om_write_u8(s_om_heap h, s_om_ref obj, s_size off, s_u8 v) {
    return s_om_write(h, obj, off, &v, 1);
}

s_om_err s_om_write_u32(s_om_heap h, s_om_ref obj, s_size off, s_u32 v) {
    return s_om_write(h, obj, off, &v, 4);
}

s_om_err s_om_write_u64(s_om_heap h, s_om_ref obj, s_size off, s_u64 v) {
    return s_om_write(h, obj, off, &v, 8);
}

/* ------------------------------------------------------------------ */
/* Stats + fault injection + test helpers                              */
/* ------------------------------------------------------------------ */

s_u64 s_om_live_bytes(s_om_heap h) {
    s_om_heap_ *hp = heap_of(h);
    return hp ? hp->live_bytes : 0;
}

s_u32 s_om_live_objects(s_om_heap h) {
    s_om_heap_ *hp = heap_of(h);
    return hp ? hp->live_objects : 0;
}

s_u32 s_om_active_count(void) {
    s_u32 n = 0;
    s_u32 i;
    for (i = 0; i < S_OM_MAX_HEAPS; ++i) {
        if (heaps[i].in_use) n++;
    }
    return n;
}

void s_om_dispose_all(void) {
    s_u32 i;
    for (i = 0; i < S_OM_MAX_HEAPS; ++i) {
        if (heaps[i].in_use) {
            (void)s_pm_release(heaps[i].slots_blk);
            (void)s_pm_release(heaps[i].obj_blk);
            memset(&heaps[i], 0, sizeof(heaps[i]));
        }
    }
    fail_alloc = false;
    alloc_fail_after = 0;
    alloc_count = 0;
}

void s_om_set_fail_alloc(bool enabled) {
    fail_alloc = enabled;
}

void s_om_set_alloc_fail_after(s_u32 n_ok) {
    alloc_fail_after = n_ok;
    alloc_count = 0;
}

s_om_err s_om_set_collection_lock(s_om_heap h, bool on) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    hp->collecting = on;
    return S_OM_OK;
}

bool s_om_is_collecting(s_om_heap h) {
    s_om_heap_ *hp = heap_of(h);
    return hp ? hp->collecting : false;
}

s_u32 s_om_test_slot_index(s_om_ref obj) {
    return ref_slot(obj);
}

s_u32 s_om_test_generation(s_om_ref obj) {
    return ref_gen(obj);
}

s_om_ref s_om_test_make_handle(s_u32 slot_index, s_u32 generation) {
    return make_ref(slot_index, generation);
}

s_om_err s_om_test_set_generation(s_om_heap h, s_u32 slot_index, s_u32 generation) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    s_om_slot sl;
    s_om_err e = slot_load(hp, slot_index, &sl);
    if (e != S_OM_OK) return e;
    if (sl.magic != 0) return S_OM_EINVAL; /* must be free */
    sl.gen = generation;
    return slot_store(hp, slot_index, &sl);
}

static s_om_err test_hdr_write(s_om_heap_ *hp, s_om_ref obj, s_size off,
                               bool is32, s_u32 v) {
    s_om_slot sl;
    s_om_err e = ref_check(hp, obj, &sl);
    if (e != S_OM_OK) return e;
    s_size need = is32 ? 4 : 1;
    if (off > S_OM_HEADER_SIZE - need) return S_OM_EINVAL;
    s_size abs = (s_size)sl.off + off;
    if (is32) {
        return pm_err_map(s_pm_write_u32(hp->obj_blk, abs, v));
    }
    return pm_err_map(s_pm_write_u8(hp->obj_blk, abs, (s_u8)v));
}

s_om_err s_om_test_set_hdr_u8(s_om_heap h, s_om_ref obj, s_size off, s_u8 v) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    return test_hdr_write(hp, obj, off, false, v);
}

s_om_err s_om_test_set_hdr_u32(s_om_heap h, s_om_ref obj, s_size off, s_u32 v) {
    s_om_heap_ *hp = heap_of(h);
    if (!hp) return S_OM_ENOENT;
    return test_hdr_write(hp, obj, off, true, v);
}
