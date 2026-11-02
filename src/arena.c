#include "suru/arena.h"

#include <string.h>

#define SU_ARENA_DEFAULT_CHUNK (8u * 1024u)
#define SU_ARENA_MIN_CHUNK     256u

static void *align_up(void *p, s_size align) {
    s_uptr a = (s_uptr)p;
    s_uptr mask = (s_uptr)(align - 1);
    s_uptr r = (a + mask) & ~mask;
    return (void *)r;
}

static s_size align_up_size(s_size n, s_size align) {
    s_size mask = align - 1;
    if (n > (s_size)-1 - mask) return (s_size)-1;
    return (n + mask) & ~mask;
}

void s_arena_init(s_arena *a) {
    a->head = NULL;
    a->current = NULL;
    a->total_allocated = 0;
}

void s_arena_dispose(s_arena *a) {
    s_arena_chunk *c = a->head;
    while (c) {
        s_arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    a->head = NULL;
    a->current = NULL;
    a->total_allocated = 0;
}

static bool chunk_new(s_arena *a, s_size mincap, s_arena_chunk **out) {
    s_size chunk = SU_ARENA_DEFAULT_CHUNK;
    while (chunk < mincap && chunk < (s_size)-1 / 2) {
        chunk *= 2;
    }
    if (chunk < mincap) chunk = mincap;
    s_size need;
    if (!s_size_add_ovf(chunk, sizeof(s_arena_chunk), &need)) return false;
    s_arena_chunk *c = malloc(need);
    if (!c) return false;
    c->next = NULL;
    c->used = 0;
    c->cap = chunk;
    /* current chunk becomes this one */
    if (a->current) {
        c->next = a->current->next;
        a->current->next = c;
    } else {
        a->head = c;
    }
    a->current = c;
    a->total_allocated += chunk;
    *out = c;
    return true;
}

void *s_arena_alloc(s_arena *a, s_size size, s_size align) {
    if (size == 0) size = 1;
    if (align == 0) align = 1;
    s_size asz = align_up_size(size, align);
    if (asz == (s_size)-1) return NULL;

    s_arena_chunk *c = a->current;
    if (c) {
        void *base = (void *)((char *)c + sizeof(s_arena_chunk));
        void *p = align_up((char *)base + c->used, align);
        s_size used = (s_size)((char *)p - (char *)base);
        if (used <= c->cap && asz <= c->cap - used) {
            c->used = used + asz;
            return p;
        }
    }
    /* need a new chunk */
    s_size mincap = sizeof(s_arena_chunk) + asz;
    s_arena_chunk *nc;
    if (!chunk_new(a, mincap, &nc)) return NULL;
    void *base = (void *)((char *)nc + sizeof(s_arena_chunk));
    void *p = align_up(base, align);
    s_size used = (s_size)((char *)p - (char *)base) + asz;
    if (used > nc->cap) {
        /* alignment pushed past capacity; allocate a dedicated chunk */
        s_size need;
        if (!s_size_add_ovf(sizeof(s_arena_chunk), used, &need)) return NULL;
        s_arena_chunk *ded = malloc(need);
        if (!ded) return NULL;
        ded->next = nc->next;
        nc->next = ded;
        a->current = ded;
        a->total_allocated += used;
        void *db = (void *)((char *)ded + sizeof(s_arena_chunk));
        ded->used = used;
        return db;
    }
    nc->used = used;
    return p;
}

void *s_arena_calloc(s_arena *a, s_size count, s_size size, s_size align) {
    s_size total;
    if (!s_size_mul_ovf(count, size, &total)) return NULL;
    void *p = s_arena_alloc(a, total, align);
    if (!p) return NULL;
    memset(p, 0, total);
    return p;
}

char *s_arena_strdup(s_arena *a, const char *s) {
    return s_arena_strndup(a, s, strlen(s));
}

char *s_arena_strndup(s_arena *a, const char *s, s_size n) {
    char *p = s_arena_alloc(a, n + 1, 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}
