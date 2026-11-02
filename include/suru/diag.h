#ifndef SURU_DIAG_H
#define SURU_DIAG_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Source location. */
typedef struct s_source_loc {
    const char *file;      /* not owned by the diagnostic context */
    s_u32 line;            /* 1-based */
    s_u32 col;             /* 1-based */
} s_source_loc;

#define S_LOC_NONE ((s_source_loc){ NULL, 0, 0 })

/* Severity. */
typedef enum s_diag_sev {
    S_DIAG_WARN = 0,
    S_DIAG_ERROR = 1,
} s_diag_sev;

/* A single diagnostic. */
typedef struct s_diag {
    s_diag_sev sev;
    s_source_loc loc;
    int code;              /* stable diagnostic code */
    char msg[512];
} s_diag;

/* Diagnostic context. Owns an array of diagnostics. */
typedef struct s_diag_ctx {
    s_diag *items;
    s_size count;
    s_size cap;
    int error_count;
    const char *src;        /* optional source text for snippets (not owned) */
    s_size srclen;
} s_diag_ctx;

void s_diag_init(s_diag_ctx *ctx);
void s_diag_dispose(s_diag_ctx *ctx);

/* Optionally attach the source text so printed diagnostics include a snippet
 * with a caret. `src` is not owned and must outlive the context. */
void s_diag_set_source(s_diag_ctx *ctx, const char *src, s_size len);

void s_diag_report(s_diag_ctx *ctx, s_diag_sev sev,
                   s_source_loc loc, int code,
                   const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

void s_diag_warn(s_diag_ctx *ctx, s_source_loc loc, int code, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
void s_diag_error(s_diag_ctx *ctx, s_source_loc loc, int code, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

bool s_diag_has_errors(const s_diag_ctx *ctx);

/* Print all diagnostics to stderr. */
void s_diag_print(const s_diag_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SURU_DIAG_H */
