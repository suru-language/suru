#ifndef SURU_PARSER_H
#define SURU_PARSER_H

#include "suru/base.h"
#include "suru/diag.h"
#include "suru/arena.h"
#include "suru/lexer.h"
#include "suru/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct s_parser {
    s_lexer lx;
    s_token cur;
    s_token ahead;           /* single-token lookahead */
    bool has_ahead;
    s_arena *arena;
    s_diag_ctx *diag;
    int depth;               /* nesting depth guard */
    s_module mod;
    s_func_decl *funcs;
    s_size nfuncs;
    s_size funcs_cap;
    s_record_decl *records;
    s_size nrecords;
    s_size records_cap;
    s_const_decl *consts;
    s_size nconsts;
    s_size consts_cap;
    char **included;         /* resolved paths of files already included */
    s_size nincluded;
    s_size included_cap;
    int file_depth;          /* include recursion guard */
} s_parser;

/* Parse a module from source text. Returns true if the parser completed and
 * no fatal allocation failure occurred. Errors are recorded in diag. The
 * resulting module's storage lives in `arena`. */
bool s_parse(s_parser *p, const char *file, const char *src, s_size len,
             s_arena *arena, s_diag_ctx *diag);

#ifdef __cplusplus
}
#endif

#endif /* SURU_PARSER_H */
