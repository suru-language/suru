#ifndef SURU_VM_H
#define SURU_VM_H

#include "suru/base.h"
#include "suru/value.h"
#include "suru/bc.h"
#include "suru/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct s_vm s_vm;

s_vm *s_vm_create(const s_bc_module *m);
void s_vm_destroy(s_vm *vm);

/* Set the program's command-line arguments (argv is borrowed, not owned). */
void s_vm_set_args(s_vm *vm, int argc, char **argv);

/* Execute the (verified) module's entry function. Returns the process exit
 * code. On a runtime trap it prints the error to stderr and returns 1. */
int s_vm_run(s_vm *vm, s_heap *heap);

bool s_vm_trapped(const s_vm *vm);
const char *s_vm_error(const s_vm *vm);

#ifdef __cplusplus
}
#endif

#endif /* SURU_VM_H */
