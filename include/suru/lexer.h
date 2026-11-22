#ifndef SURU_LEXER_H
#define SURU_LEXER_H

#include "suru/base.h"
#include "suru/diag.h"
#include "suru/token.h"
#include "suru/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A lexer over an in-memory source buffer. The source buffer must outlive
 * active scanning. Identifier spellings and decoded string-literal bytes
 * are copied into the provided arena, so token text remains valid after the
 * source buffer is released. The file-name pointer remains caller-owned. */
typedef struct s_lexer {
    const char *file;       /* file name, not owned */
    const char *src;
    s_size len;
    s_size pos;             /* byte offset */
    s_u32 line;
    s_u32 col;
    s_arena *arena;
    s_diag_ctx *diag;
    int depth;              /* block comment nesting depth */
} s_lexer;

void s_lexer_init(s_lexer *lx, const char *file, const char *src,
                  s_size len, s_arena *arena, s_diag_ctx *diag);

/* Scan the next token. Returns true on success. On a lexical error, records
 * a diagnostic and the token kind is S_TOK_EOF (caller may check). */
bool s_lexer_next(s_lexer *lx, s_token *out);

#ifdef __cplusplus
}
#endif

#endif /* SURU_LEXER_H */
