#include "suru/mem.h"
#include <string.h>

#define SU_HEAP_SMALL_MAX 4096u
#define SU_HEAP_BUCKET 16u
#define SU_HEAP_NBUCKETS (SU_HEAP_SMALL_MAX / SU_HEAP_BUCKET)  /* 256 */

typedef struct s_root_slot { s_value *slot; struct s_root_slot *next; } s_root_slot;

typedef struct s_heap {
    s_gcobj *free_buckets[SU_HEAP_NBUCKETS];
    s_gcobj *large_free;         /* singly linked free list for large chunks */
    s_gcobj *all_live;           /* live object list */
    s_value *stack_base;
    s_size stack_len;
    s_root_slot *roots;
    s_u64 live_bytes;
    s_u64 total_bytes;
    s_u64 collections;
    s_u32 live_objects;
    bool fail_alloc;
    s_u32 fail_after;
    s_u32 alloc_count;
    /* incremental marking state */
    s_gcobj **mark_stack;        /* grey worklist */
    s_size mark_len, mark_cap;
    bool mark_active;
    bool incremental;
} s_heap;

static s_size bucket_index(s_size rounded) {
    return (rounded - 1) / SU_HEAP_BUCKET;
}

static s_size round_up16(s_size n) {
    return (n + 15u) & ~(s_size)15u;
}

static bool is_small(s_size rounded) {
    return rounded <= SU_HEAP_SMALL_MAX;
}

static void *raw_alloc(s_size n) {
    return malloc(n);
}

static void raw_free(void *p) {
    free(p);
}

s_heap *s_heap_create(void) {
    s_heap *h = calloc(1, sizeof(s_heap));
    return h;
}

static void free_live_list(s_heap *h) {
    s_gcobj *o = h->all_live;
    while (o) {
        s_gcobj *next = o->next_all;
        if (is_small(o->chunk_size)) {
            /* not in a bucket; free raw */
            raw_free(o);
        } else {
            raw_free(o);
        }
        o = next;
    }
    h->all_live = NULL;
}

static void free_free_lists(s_heap *h) {
    for (s_size i = 0; i < SU_HEAP_NBUCKETS; ++i) {
        s_gcobj *o = h->free_buckets[i];
        while (o) {
            s_gcobj *next = *(s_gcobj **)o;
            raw_free(o);
            o = next;
        }
        h->free_buckets[i] = NULL;
    }
    s_gcobj *o = h->large_free;
    while (o) {
        s_gcobj *next = *(s_gcobj **)o;
        raw_free(o);
        o = next;
    }
    h->large_free = NULL;
}

void s_heap_destroy(s_heap *h) {
    if (!h) return;
    /* free stack root structs */
    s_root_slot *r = h->roots;
    while (r) {
        struct s_root_slot *next = r->next;
        raw_free(r);
        r = next;
    }
    free_live_list(h);
    free_free_lists(h);
    free(h->mark_stack);
    raw_free(h);
}

static s_gcobj *take_free(s_heap *h, s_size rounded) {
    if (is_small(rounded)) {
        s_size bi = bucket_index(rounded);
        for (s_size i = bi; i < SU_HEAP_NBUCKETS; ++i) {
            s_gcobj *o = h->free_buckets[i];
            if (o) {
                h->free_buckets[i] = *(s_gcobj **)o;
                return o;
            }
        }
        return NULL;
    }
    /* large: use first large chunk of sufficient size */
    s_gcobj **pp = &h->large_free;
    while (*pp) {
        if ((*pp)->chunk_size >= rounded) {
            s_gcobj *o = *pp;
            *pp = *(s_gcobj **)o;
            return o;
        }
        pp = &(*pp)->next_all; /* reuse link as linked-list next */
    }
    return NULL;
}

s_gcobj *s_heap_alloc(s_heap *h, s_obj_kind kind, s_u32 count, s_size payload_bytes) {
    if (!h) return NULL;
    if (h->fail_alloc || (h->fail_after > 0 && h->alloc_count >= h->fail_after)) {
        return NULL;
    }
    h->alloc_count++;

    s_size payload = payload_bytes;
    if (kind == S_OBJ_ARRAY || kind == S_OBJ_RECORD || kind == S_OBJ_CLOSURE) {
        s_size n;
        if (!s_size_mul_ovf(count, sizeof(s_value), &n)) return NULL;
        payload = n;
    }
    s_size total;
    if (!s_size_add_ovf(sizeof(s_gcobj), payload, &total)) return NULL;
    s_size rounded = round_up16(total);

    s_gcobj *o = take_free(h, rounded);
    if (!o) {
        o = raw_alloc(rounded);
        if (!o) return NULL;
    }

    /* initialize header */
    o->chunk_size = rounded;
    o->count = count;
    o->magic = S_OBJ_MAGIC;
    o->kind = (s_u8)kind;
    o->colour = S_GC_WHITE;
    o->flags = 0;
    o->next_all = h->all_live;
    h->all_live = o;
    h->live_objects++;
    h->live_bytes += rounded;
    h->total_bytes += rounded;

    /* zero payload */
    char *payloadp = (char *)(o + 1);
    memset(payloadp, 0, payload);
    /* During an active incremental marking cycle, treat the new object as
     * live by shading it grey so it will be scanned (and its children). */
    if (h->mark_active) {
        o->colour = S_GC_GREY;
        if (h->mark_len >= h->mark_cap) {
            s_size ncap = h->mark_cap ? h->mark_cap * 2 : 256;
            s_gcobj **n = realloc(h->mark_stack, ncap * sizeof(*n));
            if (!n) { fprintf(stderr, "SURU GC: mark stack allocation failed\n"); abort(); }
            h->mark_stack = n;
            h->mark_cap = ncap;
        }
        h->mark_stack[h->mark_len++] = o;
    }
    return o;
}

s_gcobj *s_heap_new_str(s_heap *h, const char *bytes, s_size len) {
    s_gcobj *o = s_heap_alloc(h, S_OBJ_STR, (s_u32)len, len);
    if (!o) return NULL;
    if (len) memcpy(s_obj_str_data(o), bytes, len);
    return o;
}

s_gcobj *s_heap_new_array(s_heap *h, s_u32 count) {
    return s_heap_alloc(h, S_OBJ_ARRAY, count, 0);
}

s_gcobj *s_heap_new_record(s_heap *h, s_u32 nfields) {
    return s_heap_alloc(h, S_OBJ_RECORD, nfields, 0);
}

void s_heap_set_stack_root(s_heap *h, s_value *base, s_size len) {
    h->stack_base = base;
    h->stack_len = len;
}

void s_heap_add_root(s_heap *h, s_value *slot) {
    struct s_root_slot *r = malloc(sizeof(*r));
    if (!r) return;
    r->slot = slot;
    r->next = h->roots;
    h->roots = r;
}

void s_heap_remove_root(s_heap *h, s_value *slot) {
    struct s_root_slot **pp = &h->roots;
    while (*pp) {
        if ((*pp)->slot == slot) {
            struct s_root_slot *gone = *pp;
            *pp = gone->next;
            free(gone);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* ------------------------------------------------------------------ */
/* Tri-color marking                                                    */
/* ------------------------------------------------------------------ */

/* Queue a white object onto the grey worklist, turning it grey. */
static void mark_push(s_heap *h, s_gcobj *o) {
    if (o->magic != S_OBJ_MAGIC) {
        /* Corrupted object pointer; abort to surface the bug. */
        fprintf(stderr, "SURU GC: corrupt object pointer\n");
        abort();
    }
    if (o->colour != S_GC_WHITE) return;
    o->colour = S_GC_GREY;
    if (h->mark_len >= h->mark_cap) {
        s_size ncap = h->mark_cap ? h->mark_cap * 2 : 256;
        s_gcobj **n = realloc(h->mark_stack, ncap * sizeof(*n));
        if (!n) {
            /* Cannot grow the mark stack: we can no longer guarantee we will
             * scan every live object, so abort rather than free live memory. */
            fprintf(stderr, "SURU GC: mark stack allocation failed\n");
            abort();
        }
        h->mark_stack = n;
        h->mark_cap = ncap;
    }
    h->mark_stack[h->mark_len++] = o;
}

static void mark_from_value(s_heap *h, s_value v) {
    if (v.tag == S_VAL_OBJ && v.u.o) {
        mark_push(h, v.u.o);
    }
}

/* Scan one grey object: mark its children grey, then blacken it. Returns
 * false when the worklist is empty (marking complete). */
static bool mark_process_one(s_heap *h) {
    if (h->mark_len == 0) return false;
    s_gcobj *o = h->mark_stack[--h->mark_len];
    if (o->kind == S_OBJ_ARRAY || o->kind == S_OBJ_RECORD ||
        o->kind == S_OBJ_CLOSURE) {
        s_value *vals = s_obj_array_data(o);
        for (s_u32 i = 0; i < o->count; ++i) {
            mark_from_value(h, vals[i]);
        }
    }
    o->colour = S_GC_BLACK;
    return true;
}

static void seed_roots(s_heap *h) {
    if (h->stack_base) {
        for (s_size i = 0; i < h->stack_len; ++i) {
            mark_from_value(h, h->stack_base[i]);
        }
    }
    for (s_root_slot *r = h->roots; r; r = r->next) {
        if (r->slot) mark_from_value(h, *r->slot);
    }
}

s_size s_heap_collect_step(s_heap *h, s_size budget) {
    if (!h) return 0;
    if (!h->mark_active) {
        /* start a new marking cycle: everything is white, then root it */
        for (s_gcobj *o = h->all_live; o; o = o->next_all) {
            o->colour = S_GC_WHITE;
        }
        h->mark_active = true;
    }
    /* Re-seed roots every slice so objects newly referenced from the mutator
     * stack after the previous slice are not missed. */
    seed_roots(h);
    s_size done = 0;
    while (done < budget && h->mark_len > 0) {
        if (!mark_process_one(h)) break;
        done++;
    }
    if (h->mark_len == 0) h->mark_active = false;
    return h->mark_len;
}

void s_heap_set_incremental(s_heap *h, bool on) {
    if (h) h->incremental = on;
}

bool s_heap_is_incremental(const s_heap *h) {
    return h ? h->incremental : false;
}

bool s_heap_marking(const s_heap *h) {
    return h ? h->mark_active : false;
}

void s_heap_write_barrier(s_heap *h, s_gcobj *dst, s_value v) {
    if (!h || !h->mark_active) return;
    if (dst->colour == S_GC_BLACK &&
        v.tag == S_VAL_OBJ && v.u.o && v.u.o->colour == S_GC_WHITE) {
        mark_push(h, v.u.o);
    }
}

static void sweep_whites(s_heap *h) {
    s_gcobj **pp = &h->all_live;
    while (*pp) {
        s_gcobj *o = *pp;
        if (o->colour == S_GC_WHITE) {
            *pp = o->next_all;
            free_object(h, o);
        } else {
            pp = &o->next_all;
        }
    }
}

bool s_heap_collect_incremental(s_heap *h, s_size budget) {
    if (!h) return true;
    s_size pending = s_heap_collect_step(h, budget);
    if (pending == 0) {
        sweep_whites(h);
        h->collections++;
        return true;
    }
    return false;
}

static void free_object(s_heap *h, s_gcobj *o) {
    h->live_objects--;
    h->live_bytes -= o->chunk_size;
    if (is_small(o->chunk_size)) {
        s_size bi = bucket_index(o->chunk_size);
        *(s_gcobj **)o = h->free_buckets[bi];
        h->free_buckets[bi] = o;
    } else {
        *(s_gcobj **)o = h->large_free;
        h->large_free = o;
    }
}

void s_heap_collect(s_heap *h) {
    if (!h) return;
    /* drain marking to completion (incremental groundwork: this is a full
     * stop-the-world collect; collect_step may be used to interleave) */
    s_heap_collect_step(h, (s_size)-1);
    /* sweep: free everything still white */
    sweep_whites(h);
    h->collections++;
}

s_u64 s_heap_live_bytes(const s_heap *h) { return h ? h->live_bytes : 0; }
s_u64 s_heap_total_bytes(const s_heap *h) { return h ? h->total_bytes : 0; }
s_u32 s_heap_live_objects(const s_heap *h) { return h ? h->live_objects : 0; }
s_u64 s_heap_collections(const s_heap *h) { return h ? h->collections : 0; }

void s_heap_set_fail_alloc(s_heap *h, bool enabled) {
    if (h) h->fail_alloc = enabled;
}
void s_heap_set_alloc_fail_after(s_heap *h, s_u32 n_ok) {
    if (h) {
        h->fail_after = n_ok;
        h->alloc_count = 0;
    }
}
