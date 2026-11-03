#ifndef SURU_BASE_H
#define SURU_BASE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Integer type aliases                                                */
/* ------------------------------------------------------------------ */

typedef int64_t  s_i64;
typedef int32_t  s_i32;
typedef int16_t  s_i16;
typedef int8_t   s_i8;
typedef uint64_t s_u64;
typedef uint32_t s_u32;
typedef uint16_t s_u16;
typedef uint8_t  s_u8;
typedef size_t   s_size;
typedef ptrdiff_t s_ptrdiff;
typedef uintptr_t s_uptr;

/* ------------------------------------------------------------------ */
/* Assertions                                                          */
/* ------------------------------------------------------------------ */

#define SU_STRINGIFY_(x) #x
#define SU_STRINGIFY(x) SU_STRINGIFY_(x)

/* printf-style format attribute; empty on compilers that reject
 * __attribute__ (e.g. MSVC). */
#if defined(__GNUC__) || defined(__clang__)
#define SU_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define SU_PRINTF(fmt_idx, arg_idx)
#endif

#if defined(SU_ENABLE_ASSERT)
#define SU_ASSERT(cond) do {                                                \
    if (!(cond)) {                                                          \
        fprintf(stderr,                                                     \
                "SURU assertion failed: %s\n  at %s:%d\n",                   \
                SU_STRINGIFY(cond), __FILE__, __LINE__);                    \
        abort();                                                            \
    }                                                                       \
} while (0)
#define SU_UNREACHABLE() do {                                               \
    fprintf(stderr, "SURU unreachable reached at %s:%d\n",                  \
            __FILE__, __LINE__);                                            \
    abort();                                                                \
} while (0)
#else
#define SU_ASSERT(cond) ((void)0)
#if defined(__GNUC__) || defined(__clang__)
#define SU_UNREACHABLE() __builtin_unreachable()
#else
#define SU_UNREACHABLE() __assume(0)
#endif
#endif

/* ------------------------------------------------------------------ */
/* Overflow-safe arithmetic                                            */
/* ------------------------------------------------------------------ */
/* All helpers return true on success. On overflow they return false    */
/* and leave *out untouched. These never invoke undefined behavior.     */
/* GCC/Clang use the __builtin_*_overflow intrinsics; MSVC (no such     */
/* builtins) uses portable checks against the limits.                   */

#if defined(__GNUC__) || defined(__clang__)
#define SU_HAVE_BUILTIN_OVERFLOW 1
#endif

static inline bool s_i64_add_ovf(s_i64 a, s_i64 b, s_i64 *out) {
#ifdef SU_HAVE_BUILTIN_OVERFLOW
    return !__builtin_add_overflow(a, b, out);
#else
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return false;
    *out = a + b;
    return true;
#endif
}
static inline bool s_i64_sub_ovf(s_i64 a, s_i64 b, s_i64 *out) {
#ifdef SU_HAVE_BUILTIN_OVERFLOW
    return !__builtin_sub_overflow(a, b, out);
#else
    if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b))
        return false;
    *out = a - b;
    return true;
#endif
}
static inline bool s_i64_mul_ovf(s_i64 a, s_i64 b, s_i64 *out) {
#ifdef SU_HAVE_BUILTIN_OVERFLOW
    return !__builtin_mul_overflow(a, b, out);
#else
    if (a == 0 || b == 0) { *out = 0; return true; }
    if (a == -1)           { if (b == INT64_MIN) return false; *out = -b; return true; }
    if (b == -1)           { if (a == INT64_MIN) return false; *out = -a; return true; }
    if (a > 0) {
        if (b > 0) { if (a > INT64_MAX / b) return false; }
        else       { if (b < INT64_MIN / a) return false; }
    } else {
        if (b > 0) { if (a < INT64_MIN / b) return false; }
        else       { if (a < INT64_MAX / b) return false; }
    }
    *out = a * b;
    return true;
#endif
}
static inline bool s_i64_div_ovf(s_i64 a, s_i64 b, s_i64 *out) {
    if (b == 0) return false;
    if (a == INT64_MIN && b == -1) return false;
    *out = a / b;
    return true;
}
static inline bool s_i32_add_ovf(s_i32 a, s_i32 b, s_i32 *out) {
#ifdef SU_HAVE_BUILTIN_OVERFLOW
    return !__builtin_add_overflow(a, b, out);
#else
    if ((b > 0 && a > INT32_MAX - b) || (b < 0 && a < INT32_MIN - b))
        return false;
    *out = a + b;
    return true;
#endif
}
static inline bool s_u32_add_ovf(s_u32 a, s_u32 b, s_u32 *out) {
#ifdef SU_HAVE_BUILTIN_OVERFLOW
    return !__builtin_add_overflow(a, b, out);
#else
    if (UINT32_MAX - a < b) return false;
    *out = a + b;
    return true;
#endif
}
static inline bool s_size_add_ovf(s_size a, s_size b, s_size *out) {
#ifdef SU_HAVE_BUILTIN_OVERFLOW
    return !__builtin_add_overflow(a, b, out);
#else
    if (SIZE_MAX - a < b) return false;
    *out = a + b;
    return true;
#endif
}
static inline bool s_size_mul_ovf(s_size a, s_size b, s_size *out) {
#ifdef SU_HAVE_BUILTIN_OVERFLOW
    return !__builtin_mul_overflow(a, b, out);
#else
    if (a == 0 || b == 0) { *out = 0; return true; }
    if (a > SIZE_MAX / b) return false;
    *out = a * b;
    return true;
#endif
}
static inline bool s_i64_neg_ovf(s_i64 a, s_i64 *out) {
    if (a == INT64_MIN) return false;
    *out = -a;
    return true;
}

/* Saturating add for counters that must never wrap. */
static inline s_u32 s_u32_saturating_add(s_u32 a, s_u32 b) {
    if (UINT32_MAX - a < b) return UINT32_MAX;
    return a + b;
}

#endif /* SURU_BASE_H */
