#ifndef SURU_TOKEN_H
#define SURU_TOKEN_H

#include "suru/base.h"
#include "suru/diag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum s_tok_kind {
    S_TOK_EOF = 0,

    /* identifiers and literals */
    S_TOK_IDENT,
    S_TOK_INT,
    S_TOK_FLOAT,
    S_TOK_STR,
    S_TOK_TRUE,
    S_TOK_FALSE,
    S_TOK_NONE,

    /* keywords */
    S_TOK_KW_BREAK,
    S_TOK_KW_CONTINUE,
    S_TOK_KW_ELSE,
    S_TOK_KW_FOR,
    S_TOK_KW_FUNC,
    S_TOK_KW_IF,
    S_TOK_KW_IN,
    S_TOK_KW_LET,
    S_TOK_KW_MUT,
    S_TOK_KW_RETURN,
    S_TOK_KW_TYPE,
    S_TOK_KW_USE,
    S_TOK_KW_WHILE,
    S_TOK_KW_NEW,
    S_TOK_KW_AS,
    S_TOK_KW_CONST,

    /* punctuation and operators */
    S_TOK_LPAREN,
    S_TOK_RPAREN,
    S_TOK_LBRACE,
    S_TOK_RBRACE,
    S_TOK_LBRACKET,
    S_TOK_RBRACKET,
    S_TOK_COMMA,
    S_TOK_SEMI,
    S_TOK_DOT,
    S_TOK_COLON,
    S_TOK_ARROW,          /* -> */

    S_TOK_ASSIGN,         /* = */
    S_TOK_PLUS,           /* + */
    S_TOK_MINUS,          /* - */
    S_TOK_STAR,           /* * */
    S_TOK_SLASH,          /* / */
    S_TOK_PERCENT,        /* % */
    S_TOK_EQ,             /* == */
    S_TOK_NE,             /* != */
    S_TOK_LT,             /* < */
    S_TOK_LE,             /* <= */
    S_TOK_GT,             /* > */
    S_TOK_GE,             /* >= */
    S_TOK_AND,            /* and */
    S_TOK_OR,             /* or */
    S_TOK_NOT,            /* not */
    S_TOK_PLUS_ASSIGN,    /* += */
    S_TOK_MINUS_ASSIGN,   /* -= */
    S_TOK_STAR_ASSIGN,    /* *= */
    S_TOK_SLASH_ASSIGN,   /* /= */
} s_tok_kind;

typedef struct s_token {
    s_tok_kind kind;
    s_source_loc loc;
    union {
        struct {
            const char *text;
            s_size len;
        } s;                 /* identifier text or decoded string bytes */
        s_i64 i;             /* integer literal value */
        double f;            /* float literal value */
    } v;
} s_token;

/* Utility: human-readable name for a token kind (for messages). */
const char *s_tok_kind_name(s_tok_kind k);

#ifdef __cplusplus
}
#endif

#endif /* SURU_TOKEN_H */
