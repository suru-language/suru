#ifndef SURU_BCC_H
#define SURU_BCC_H

#include "suru/base.h"
#include "suru/diag.h"
#include "suru/arena.h"
#include "suru/ast.h"
#include "suru/bc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lower a typed AST into a bytecode module. The module is owned by the
 * caller and must be disposed with s_bc_module_dispose. */
bool s_bcc_run(const s_module *ast, s_arena *arena, s_diag_ctx *diag,
               s_bc_module *out);

#ifdef __cplusplus
}
#endif

#endif /* SURU_BCC_H */
