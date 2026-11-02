#ifndef SURU_DRIVER_H
#define SURU_DRIVER_H

#include "suru/base.h"
#include "suru/bc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compile source text to a bytecode module. Returns 0 on success; on failure
 * prints diagnostics to stderr and returns -1. On success *out must be
 * disposed with s_bc_module_dispose. */
int s_driver_compile(const char *file, const char *src, s_size len,
                     s_bc_module *out);

int s_driver_run_module(s_bc_module *m);

#ifdef __cplusplus
}
#endif

#endif /* SURU_DRIVER_H */
