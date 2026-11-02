#ifndef SURU_ARENA_H
#define SURU_ARENA_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A bump-pointer arena allocator used by the compiler frontend.
 * Allocations are freed in bulk when the arena is disposed. No per-object
 * free is performed. Memory is never returned to the system early. */

typedef struct s_arena_chunk {
    struct s_arena_chunk *next;
    s_size used;
    s_size cap;
    /* payload follows this header, aligned to max_align_t */
} s_arena_chunk;

typedef struct s_arena {
    s_arena_chunk *head;
    s_arena_chunk *current;
    s_size total_allocated;
} s_arena;

void s_arena_init(s_arena *a);
void s_arena_dispose(s_arena *a);

/* Allocate `size` bytes aligned to `align` (must be a power of two).
 * Returns NULL on allocation failure. */
void *s_arena_alloc(s_arena *a, s_size size, s_size align);

/* Allocate `count` items of `size` bytes each, zero-filled. */
void *s_arena_calloc(s_arena *a, s_size count, s_size size, s_size align);

char *s_arena_strdup(s_arena *a, const char *s);
char *s_arena_strndup(s_arena *a, const char *s, s_size n);

#ifdef __cplusplus
}
#endif

#endif /* SURU_ARENA_H */
