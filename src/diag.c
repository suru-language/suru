#include "suru/diag.h"

#include <stdarg.h>
#include <string.h>

void s_diag_init(s_diag_ctx *ctx) {
    ctx->items = NULL;
    ctx->count = 0;
    ctx->cap = 0;
    ctx->error_count = 0;
    ctx->src = NULL;
    ctx->srclen = 0;
}

void s_diag_set_source(s_diag_ctx *ctx, const char *src, s_size len) {
    ctx->src = src;
    ctx->srclen = len;
}

void s_diag_dispose(s_diag_ctx *ctx) {
    free(ctx->items);
    ctx->items = NULL;
    ctx->count = 0;
    ctx->cap = 0;
    ctx->error_count = 0;
}

static bool diag_reserve(s_diag_ctx *ctx, s_size want) {
    if (want <= ctx->cap) return true;
    s_size ncap = ctx->cap ? ctx->cap : 8;
    while (ncap < want) {
        s_size next;
        if (!s_size_mul_ovf(ncap, 2, &next)) return false;
        ncap = next;
    }
    s_size bytes;
    if (!s_size_mul_ovf(ncap, sizeof(s_diag), &bytes)) return false;
    s_diag *nitems = realloc(ctx->items, bytes);
    if (!nitems) return false;
    ctx->items = nitems;
    ctx->cap = ncap;
    return true;
}

static void diag_vreport(s_diag_ctx *ctx, s_diag_sev sev,
                         s_source_loc loc, int code,
                         const char *fmt, va_list ap) {
    if (!diag_reserve(ctx, ctx->count + 1)) return;
    s_diag *d = &ctx->items[ctx->count];
    d->sev = sev;
    d->loc = loc;
    d->code = code;
    vsnprintf(d->msg, sizeof(d->msg), fmt, ap);
    if (sev == S_DIAG_ERROR) ++ctx->error_count;
    ++ctx->count;
}

void s_diag_report(s_diag_ctx *ctx, s_diag_sev sev,
                   s_source_loc loc, int code,
                   const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_vreport(ctx, sev, loc, code, fmt, ap);
    va_end(ap);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#endif

void s_diag_warn(s_diag_ctx *ctx, s_source_loc loc, int code,
                 const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_vreport(ctx, S_DIAG_WARN, loc, code, fmt, ap);
    va_end(ap);
}

void s_diag_error(s_diag_ctx *ctx, s_source_loc loc, int code,
                  const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_vreport(ctx, S_DIAG_ERROR, loc, code, fmt, ap);
    va_end(ap);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

bool s_diag_has_errors(const s_diag_ctx *ctx) {
    return ctx->error_count > 0;
}

static void print_ctx_line(const char *s, s_size n, s_size start, s_size end,
                           s_u32 lineno) {
    fprintf(stderr, "%5u | ", lineno);
    s_size i = start;
    while (i < end && i < n) {
        char c = s[i];
        if (c == '\n' || c == '\r') break;
        fputc(c, stderr);
        i++;
    }
    fputc('\n', stderr);
}

static void print_snippet(const s_diag_ctx *ctx, const s_diag *d) {
    if (!ctx->src || d->loc.line == 0) return;
    const char *s = ctx->src;
    s_size n = ctx->srclen;
    s_size pos = 0;
    s_u32 cur = 1;
    while (pos < n && cur < d->loc.line) {
        if (s[pos] == '\n') cur++;
        pos++;
    }
    if (cur != d->loc.line) return;
    s_size ts = pos;
    s_size te = ts;
    while (te < n && s[te] != '\n') te++;
    s_size ps = ts;
    if (ts > 0 && s[ts - 1] == '\n') {
        s_size p = ts - 1;
        while (p > 0 && s[p - 1] != '\n') p--;
        ps = p;
    }
    s_size ns = te + 1;
    s_size ne = ns;
    while (ne < n && s[ne] != '\n') ne++;

    if (ps < ts) print_ctx_line(s, n, ps, ts, d->loc.line - 1);
    print_ctx_line(s, n, ts, te, d->loc.line);

    s_size cap = te - ts;
    s_u32 col = d->loc.col;
    if (col < 1) col = 1;
    fprintf(stderr, "      | ");
    s_u32 i;
    for (i = 1; i < col && i <= (s_u32)cap; ++i) fputc(' ', stderr);
    fputc('^', stderr);
    fputc('\n', stderr);

    if (ns < n) print_ctx_line(s, n, ns, ne, d->loc.line + 1);
}

void s_diag_print(const s_diag_ctx *ctx) {
    for (s_size i = 0; i < ctx->count; ++i) {
        const s_diag *d = &ctx->items[i];
        if (d->loc.file) {
            fprintf(stderr, "%s:%u:%u: ", d->loc.file, d->loc.line, d->loc.col);
        } else {
            fprintf(stderr, "suru: ");
        }
        fprintf(stderr, "%s[%d]: %s\n",
                d->sev == S_DIAG_ERROR ? "error" : "warning",
                d->code, d->msg);
        print_snippet(ctx, d);
    }
}
