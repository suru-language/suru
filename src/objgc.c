#include "suru/objgc.h"

#include <stdlib.h>
#include <string.h>

struct s_om_gc {
    s_om_heap h;
    s_om_ref *roots;
    s_size nroots;
    s_size caproots;
    const s_om_ref *stk_base;
    s_size stk_len;
    const s_value *sv_base;
    s_size sv_len;
    bool marking_active;
    s_u32 mark_cursor;
    bool pass_changed;
};

/* ------------------------------------------------------------------ */
/* Collector lifecycle                                                 */
/* ------------------------------------------------------------------ */

s_om_err s_om_gc_create(s_om_heap h, s_om_gc **out) {
    if (!out) return S_OM_EINVAL;
    *out = NULL;
    if (s_om_heap_validate(h) != S_OM_OK) return S_OM_ENOENT;
    s_om_gc *g = (s_om_gc *)calloc(1, sizeof(*g));
    if (!g) return S_OM_ENOMEM;
    g->h = h;
    *out = g;
    return S_OM_OK;
}

void s_om_gc_destroy(s_om_gc *g) {
    if (!g) return;
    free(g->roots);
    free(g);
}

/* ------------------------------------------------------------------ */
/* Roots                                                               */
/* ------------------------------------------------------------------ */

s_om_err s_om_gc_add_root(s_om_gc *g, s_om_ref obj) {
    if (!g) return S_OM_ENOENT;
    if (g->marking_active) return S_OM_ECOLLECT;
    if (obj == S_OM_REF_NONE) return S_OM_EINVAL;
    s_size i;
    for (i = 0; i < g->nroots; ++i) {
        if (g->roots[i] == obj) return S_OM_OK; /* already present */
    }
    if (g->nroots == g->caproots) {
        s_size ncap = g->caproots ? g->caproots * 2 : 8;
        if (ncap < g->caproots) return S_OM_EOVERFLOW; /* overflow guard */
        s_om_ref *nr = (s_om_ref *)realloc(g->roots, ncap * sizeof(*nr));
        if (!nr) return S_OM_ENOMEM;
        g->roots = nr;
        g->caproots = ncap;
    }
    g->roots[g->nroots++] = obj;
    return S_OM_OK;
}

s_om_err s_om_gc_remove_root(s_om_gc *g, s_om_ref obj) {
    if (!g) return S_OM_ENOENT;
    if (g->marking_active) return S_OM_ECOLLECT;
    s_size i;
    for (i = 0; i < g->nroots; ++i) {
        if (g->roots[i] == obj) {
            g->roots[i] = g->roots[g->nroots - 1];
            g->nroots--;
            return S_OM_OK;
        }
    }
    return S_OM_ENOENT;
}

void s_om_gc_clear_roots(s_om_gc *g) {
    if (g) g->nroots = 0;
}

s_om_err s_om_gc_set_stack_root(s_om_gc *g, const s_om_ref *base, s_size n) {
    if (!g) return S_OM_ENOENT;
    if (g->marking_active) return S_OM_ECOLLECT;
    if (n > 0 && !base) return S_OM_EINVAL;
    g->stk_base = base;
    g->stk_len = n;
    return S_OM_OK;
}

s_om_err s_om_gc_set_svalue_root(s_om_gc *g, const s_value *base, s_size n) {
    if (!g) return S_OM_ENOENT;
    if (g->marking_active) return S_OM_ECOLLECT;
    if (n > 0 && !base) return S_OM_EINVAL;
    g->sv_base = base;
    g->sv_len = n;
    return S_OM_OK;
}

s_u32 s_om_gc_nroots(const s_om_gc *g) {
    return g ? (s_u32)g->nroots : 0;
}

bool s_om_gc_marking(const s_om_gc *g) {
    return g ? g->marking_active : false;
}

/* ------------------------------------------------------------------ */
/* Mark-sweep                                                          */
/* ------------------------------------------------------------------ */

static bool gc_mark_children(s_om_heap h, s_om_ref obj) {
    /* Mark white children of `obj` grey. Children are stored as full object
     * handles (u64) at payload word i (byte offset i*8). Returns true if any
     * child was newly shaded grey. Stale/invalid child handles are ignored. */
    s_obj_kind k;
    s_u32 cnt;
    if (s_om_kind(h, obj, &k) != S_OM_OK || k == S_OBJ_STR) return false;
    if (s_om_count(h, obj, &cnt) != S_OM_OK) return false;
    bool touched = false;
    s_u32 i;
    for (i = 0; i < cnt; ++i) {
        s_om_ref c = S_OM_REF_NONE;
        if (s_om_read_u64(h, obj, (s_size)i * 8, &c) != S_OM_OK) break;
        if (c == S_OM_REF_NONE) continue;
        if (!s_om_is_live(h, c)) continue;
        s_u8 cc = 0;
        if (s_om_colour(h, c, &cc) != S_OM_OK) continue;
        if (cc == S_GC_WHITE) {
            (void)s_om_set_colour(h, c, S_GC_GREY);
            touched = true;
        }
    }
    return touched;
}

static void gc_reset_all_white(s_om_heap h) {
    s_u32 cur = 0;
    s_om_ref o = S_OM_REF_NONE;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) {
        s_u8 col = 0;
        if (s_om_colour(h, o, &col) == S_OM_OK && col != S_GC_WHITE) {
            (void)s_om_set_colour(h, o, S_GC_WHITE);
        }
    }
}

static void gc_seed_one(s_om_heap h, s_om_ref r) {
    if (!s_om_is_live(h, r)) return;
    s_u8 col = 0;
    if (s_om_colour(h, r, &col) == S_OM_OK && col == S_GC_WHITE) {
        (void)s_om_set_colour(h, r, S_GC_GREY);
    }
}

static void gc_seed_roots(s_om_gc *g) {
    s_om_heap h = g->h;
    s_size i;
    for (i = 0; i < g->nroots; ++i) {
        gc_seed_one(h, g->roots[i]);
    }
    for (i = 0; i < g->stk_len; ++i) {
        gc_seed_one(h, g->stk_base[i]);
    }
    for (i = 0; i < g->sv_len; ++i) {
        /* only genuine object-bearing values are treated as handles */
        if (g->sv_base[i].tag == S_VAL_OBJ) {
            gc_seed_one(h, g->sv_base[i].u.idx);
        }
    }
}

s_om_err s_om_gc_mark_begin(s_om_gc *g) {
    if (!g) return S_OM_ENOENT;
    if (g->marking_active) return S_OM_ECOLLECT;
    if (s_om_is_collecting(g->h)) return S_OM_ECOLLECT;
    (void)s_om_set_collection_lock(g->h, true);
    gc_reset_all_white(g->h);
    gc_seed_roots(g);
    g->marking_active = true;
    g->mark_cursor = 0;
    g->pass_changed = false;
    return S_OM_OK;
}

bool s_om_gc_mark_step(s_om_gc *g, s_size budget) {
    if (!g || !g->marking_active) return false;
    if (budget < 1) budget = 1;
    s_om_heap h = g->h;
    s_size examined = 0;
    while (examined < budget) {
        s_om_ref o = S_OM_REF_NONE;
        if (s_om_next_live(h, &g->mark_cursor, &o) != S_OM_OK) {
            if (g->pass_changed) {
                g->pass_changed = false;
                g->mark_cursor = 0;
                continue;
            }
            g->marking_active = false;
            (void)s_om_set_collection_lock(h, false);
            break;
        }
        s_u8 col = 0;
        if (s_om_colour(h, o, &col) == S_OM_OK && col == S_GC_GREY) {
            (void)s_om_set_colour(h, o, S_GC_BLACK);
            g->pass_changed = true;
            (void)gc_mark_children(h, o);
        }
        examined++;
    }
    return g->marking_active;
}

s_u32 s_om_gc_sweep(s_om_gc *g) {
    if (!g) return 0;
    if (g->marking_active) return 0; /* incomplete pass never treated as done */
    s_om_heap h = g->h;
    s_u32 reclaimed = 0;
    s_u32 cur = 0;
    s_om_ref o = S_OM_REF_NONE;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) {
        s_u8 col = 0;
        if (s_om_colour(h, o, &col) == S_OM_OK && col == S_GC_WHITE) {
            if (s_om_free(h, o) == S_OM_OK) reclaimed++;
        }
    }
    cur = 0;
    o = S_OM_REF_NONE;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) {
        s_u8 col = 0;
        if (s_om_colour(h, o, &col) == S_OM_OK && col == S_GC_BLACK) {
            (void)s_om_set_colour(h, o, S_GC_WHITE);
        }
    }
    g->marking_active = false;
    return reclaimed;
}

s_u32 s_om_gc_collect(s_om_gc *g) {
    if (!g) return 0;
    if (s_om_gc_mark_begin(g) != S_OM_OK) return 0;
    while (s_om_gc_mark_step(g, (s_size)-1)) {
    }
    return s_om_gc_sweep(g);
}
