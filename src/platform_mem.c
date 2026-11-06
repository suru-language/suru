#include "suru/platform_mem.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Registry                                                            */
/* ------------------------------------------------------------------ */

#define S_PM_MAX_REGIONS 256u
#define S_PM_MAGIC 0xA11A5EEDu
#define S_PM_DEFAULT_ALIGN 16u

typedef struct s_pm_region {
    void *alloc;          /* pointer passed to free() */
    unsigned char *base;   /* aligned usable base */
    s_size size;          /* usable size in bytes */
    s_size align;         /* alignment of base */
    s_u32 magic;          /* liveness stamp */
    bool in_use;
} s_pm_region;

static s_pm_region regions[S_PM_MAX_REGIONS];
static bool fail_alloc = false;
static s_u32 alloc_fail_after = 0;
static s_u32 alloc_count = 0;

static s_pm_region *region_of(s_pm_handle h) {
    if (h == 0) return NULL;
    s_u32 idx = h - 1;
    if (idx >= S_PM_MAX_REGIONS) return NULL;
    s_pm_region *r = &regions[idx];
    if (!r->in_use || r->magic != S_PM_MAGIC) return NULL;
    return r;
}

static bool is_pow2(s_size v) {
    return v != 0 && (v & (v - 1)) == 0;
}

static s_size align_up(s_size v, s_size a) {
    s_size mask = a - 1;
    return (v + mask) & ~mask;
}

static s_pm_err run_bounds(s_pm_region *r, s_size off, s_size len) {
    if (off > r->size) return S_PM_EOOB;
    if (len > r->size - off) return S_PM_EOOB;
    return S_PM_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

s_pm_err s_pm_acquire(s_size size, s_size align, s_pm_handle *out) {
    if (!out) return S_PM_EINVAL;
    *out = 0;
    if (fail_alloc || (alloc_fail_after > 0 && alloc_count >= alloc_fail_after)) {
        return S_PM_ENOMEM;
    }
    if (align == 0) align = S_PM_DEFAULT_ALIGN;
    if (!is_pow2(align)) return S_PM_EINVAL;

    /* Find a free slot first. */
    s_u32 idx;
    bool found = false;
    for (idx = 0; idx < S_PM_MAX_REGIONS; ++idx) {
        if (!regions[idx].in_use) { found = true; break; }
    }
    if (!found) return S_PM_ELIMIT;

    /* needed = align-1 + size, overflow-checked; reserve at least 1 byte. */
    s_size slack = align - 1;
    if (size > (s_size)-1 - slack) return S_PM_EOVERFLOW;
    s_size needed = size + slack;
    if (needed < 1) needed = 1;

    void *mem = malloc(needed);
    if (!mem) return S_PM_ENOMEM;

    s_uptr raw = (s_uptr)mem;
    s_uptr base = align_up(raw, align);
    (void)base;

    s_pm_region *r = &regions[idx];
    r->alloc = mem;
    r->base = (unsigned char *)(align_up(raw, align));
    r->size = size;
    r->align = align;
    r->magic = S_PM_MAGIC;
    r->in_use = true;
    alloc_count++;

    *out = (s_pm_handle)(idx + 1);
    return S_PM_OK;
}

s_pm_err s_pm_release(s_pm_handle h) {
    s_pm_region *r = region_of(h);
    if (!r) return S_PM_ENOENT;
    free(r->alloc);
    memset(r, 0, sizeof(*r));
    return S_PM_OK;
}

s_pm_err s_pm_size(s_pm_handle h, s_size *out) {
    s_pm_region *r = region_of(h);
    if (!r) return S_PM_ENOENT;
    if (out) *out = r->size;
    return S_PM_OK;
}

s_pm_err s_pm_align(s_pm_handle h, s_size *out) {
    s_pm_region *r = region_of(h);
    if (!r) return S_PM_ENOENT;
    if (out) *out = r->align;
    return S_PM_OK;
}

s_pm_err s_pm_validate(s_pm_handle h) {
    return region_of(h) ? S_PM_OK : S_PM_ENOENT;
}

s_pm_err s_pm_read(s_pm_handle h, s_size off, void *dst, s_size len) {
    s_pm_region *r = region_of(h);
    if (!r) return S_PM_ENOENT;
    if (!dst) return S_PM_EINVAL;
    if (len == 0) return S_PM_OK;
    s_pm_err e = run_bounds(r, off, len);
    if (e != S_PM_OK) return e;
    memcpy(dst, r->base + off, len);
    return S_PM_OK;
}

s_pm_err s_pm_write(s_pm_handle h, s_size off, const void *src, s_size len) {
    s_pm_region *r = region_of(h);
    if (!r) return S_PM_ENOENT;
    if (!src) return S_PM_EINVAL;
    if (len == 0) return S_PM_OK;
    s_pm_err e = run_bounds(r, off, len);
    if (e != S_PM_OK) return e;
    memcpy(r->base + off, src, len);
    return S_PM_OK;
}

s_pm_err s_pm_read_u8(s_pm_handle h, s_size off, s_u8 *out) {
    return s_pm_read(h, off, out, 1);
}

s_pm_err s_pm_read_u32(s_pm_handle h, s_size off, s_u32 *out) {
    return s_pm_read(h, off, out, 4);
}

s_pm_err s_pm_read_u64(s_pm_handle h, s_size off, s_u64 *out) {
    return s_pm_read(h, off, out, 8);
}

s_pm_err s_pm_write_u8(s_pm_handle h, s_size off, s_u8 v) {
    return s_pm_write(h, off, &v, 1);
}

s_pm_err s_pm_write_u32(s_pm_handle h, s_size off, s_u32 v) {
    return s_pm_write(h, off, &v, 4);
}

s_pm_err s_pm_write_u64(s_pm_handle h, s_size off, s_u64 v) {
    return s_pm_write(h, off, &v, 8);
}

void s_pm_set_fail_alloc(bool enabled) {
    fail_alloc = enabled;
}

void s_pm_set_alloc_fail_after(s_u32 n_ok) {
    alloc_fail_after = n_ok;
    alloc_count = 0;
}

s_u32 s_pm_active_count(void) {
    s_u32 n = 0;
    s_u32 i;
    for (i = 0; i < S_PM_MAX_REGIONS; ++i) {
        if (regions[i].in_use) n++;
    }
    return n;
}

void s_pm_dispose_all(void) {
    s_u32 i;
    for (i = 0; i < S_PM_MAX_REGIONS; ++i) {
        if (regions[i].in_use) {
            free(regions[i].alloc);
            memset(&regions[i], 0, sizeof(regions[i]));
        }
    }
    fail_alloc = false;
    alloc_fail_after = 0;
    alloc_count = 0;
}
