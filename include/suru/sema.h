#ifndef SURU_SEMA_H
#define SURU_SEMA_H

#include "suru/base.h"
#include "suru/diag.h"
#include "suru/arena.h"
#include "suru/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Run semantic analysis over the module. Resolves names, infers and checks
 * types, and produces a typed AST. Returns true if no errors were recorded.
 * The AST is mutated in place; all storage lives in the module's arena. */
bool s_sema_run(s_module *mod, s_arena *arena, s_diag_ctx *diag);

#ifdef __cplusplus
}
#endif

#endif /* SURU_SEMA_H */
