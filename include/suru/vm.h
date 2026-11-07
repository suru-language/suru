#ifndef SURU_VM_H
#define SURU_VM_H

#include "suru/base.h"
#include "suru/value.h"
#include "suru/bc.h"
#include "suru/mem.h"
#include "suru/objgc.h"

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
int s_vm_run(s_vm *vm);

bool s_vm_trapped(const s_vm *vm);
const char *s_vm_error(const s_vm *vm);

/*
 * Stage 11C: optional stop-the-world collector binding. The VM does not own the
 * collector; bind()/unbind() only attach/clear an external objgc collector.
 * sync() re-registers the VM's CURRENT live operand-stack range [0, sp) with the
 * bound collector (value-aware: only S_VAL_OBJ entries become roots). It must be
 * called at every collection boundary and after any stack growth/relocation. It
 * is a no-op (S_OM_OK) when no collector is bound. These are inert until the VM's
 * object values are objmodel handles; they do not change legacy behavior.
 */
void s_vm_gc_bind(s_vm *vm, s_om_gc *gc);
void s_vm_gc_unbind(s_vm *vm);
s_om_err s_vm_gc_sync(s_vm *vm);

#ifdef __cplusplus
}
#endif

#endif /* SURU_VM_H */
