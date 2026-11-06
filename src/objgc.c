#include "suru/objgc.h"

#include <stdlib.h>
#include <string.h>

struct s_om_gc {
    s_om_heap h;
    s_u32 *roots;
    s_size nroots;
    s_size caproots;
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

s_om_err s_om_gc_add_root(s_om_gc *g, s_u32 obj) {
    if (!g) return S_OM_ENOENT;
    if (obj == 0) return S_OM_EINVAL;
    s_size i;
    for (i = 0; i < g->nroots; ++i) {
        if (g->roots[i] == obj) return S_OM_OK; /* already present */
    }
    if (g->nroots == g->caproots) {
        s_size ncap = g->caproots ? g->caproots * 2 : 8;
        if (ncap < g->caproots) return S_OM_EOVERFLOW; /* overflow guard */
        s_u32 *nr = (s_u32 *)realloc(g->roots, ncap * sizeof(*nr));
        if (!nr) return S_OM_ENOMEM;
        g->roots = nr;
        g->caproots = ncap;
    }
    g->roots[g->nroots++] = obj;
    return S_OM_OK;
}

s_om_err s_om_gc_remove_root(s_om_gc *g, s_u32 obj) {
    if (!g) return S_OM_ENOENT;
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

s_u32 s_om_gc_nroots(const s_om_gc *g) {
    return g ? (s_u32)g->nroots : 0;
}

/* ------------------------------------------------------------------ */
/* Mark-sweep                                                          */
/* ------------------------------------------------------------------ */

static bool gc_mark_children(s_om_heap h, s_u32 obj) {
    /* Mark white children of `obj` grey. Returns true if any child was newly
     * shaded grey. */
    s_obj_kind k;
    s_u32 cnt;
    if (s_om_kind(h, obj, &k) != S_OM_OK || k == S_OBJ_STR) return false;
    if (s_om_count(h, obj, &cnt) != S_OM_OK) return false;
    bool touched = false;
    s_u32 i;
    for (i = 0; i < cnt; ++i) {
        s_u32 c = 0;
        if (s_om_read_u32(h, obj, (s_size)i * 4, &c) != S_OM_OK) break;
        if (c == 0) continue;
        if (s_om_validate(h, c) != S_OM_OK) continue;
        s_u8 cc = 0;
        if (s_om_colour(h, c, &cc) != S_OM_OK) continue;
        if (cc == S_GC_WHITE) {
            (void)s_om_set_colour(h, c, S_GC_GREY);
            touched = true;
        }
    }
    return touched;
}

s_u32 s_om_gc_collect(s_om_gc *g) {
    if (!g) return 0;
    s_om_heap h = g->h;

    /* 1. Reset every live object to white. */
    s_u32 cur = 0, o = 0;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) {
        s_u8 col = 0;
        if (s_om_colour(h, o, &col) == S_OM_OK && col != S_GC_WHITE) {
            (void)s_om_set_colour(h, o, S_GC_WHITE);
        }
    }

    /* 2. Seed roots grey. */
    {
        s_size i;
        for (i = 0; i < g->nroots; ++i) {
            s_u32 r = g->roots[i];
            if (s_om_validate(h, r) != S_OM_OK) continue;
            s_u8 col = 0;
            if (s_om_colour(h, r, &col) == S_OM_OK && col == S_GC_WHITE) {
                (void)s_om_set_colour(h, r, S_GC_GREY);
            }
        }
    }

    /* 3. Mark to fixpoint: grey -> black; white children become grey. */
    for (;;) {
        bool any = false;
        s_u32 c2 = 0, o2 = 0;
        while (s_om_next_live(h, &c2, &o2) == S_OM_OK) {
            s_u8 col = 0;
            if (s_om_colour(h, o2, &col) != S_OM_OK || col != S_GC_GREY) continue;
            (void)s_om_set_colour(h, o2, S_GC_BLACK);
            any = true;
            (void)gc_mark_children(h, o2);
        }
        if (!any) break;
    }

    /* 4. Sweep white (unreachable) live objects. */
    s_u32 reclaimed = 0;
    cur = 0;
    o = 0;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) {
        s_u8 col = 0;
        if (s_om_colour(h, o, &col) == S_OM_OK && col == S_GC_WHITE) {
            if (s_om_free(h, o) == S_OM_OK) reclaimed++;
        }
    }

    /* 5. Reset surviving (black) objects to white for the next cycle. */
    cur = 0;
    o = 0;
    while (s_om_next_live(h, &cur, &o) == S_OM_OK) {
        s_u8 col = 0;
        if (s_om_colour(h, o, &col) == S_OM_OK && col == S_GC_BLACK) {
            (void)s_om_set_colour(h, o, S_GC_WHITE);
        }
    }

    return reclaimed;
}
