#ifndef SURU_VERIFY_H
#define SURU_VERIFY_H

#include "suru/base.h"
#include "suru/bc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Verifies every function of a bytecode module. Returns 0 if the module is
 * structurally sound, otherwise a negative code. The VM must not execute a
 * module that fails verification. */
int s_verify_module(const s_bc_module *m);

/* Compute the maximum operand-stack depth reachable in function fid using a
 * control-flow worklist. Returns 0 on success (with *outmax set) or a
 * negative error code. This is shared by the compiler (to size frames) and
 * the verifier (to bound its own analysis). */
int s_verify_compute_func_depth(const s_bc_module *m, s_u32 fid,
                                s_u32 *outmax);

#ifdef __cplusplus
}
#endif

#endif /* SURU_VERIFY_H */
