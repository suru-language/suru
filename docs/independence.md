# C-Independence Checklist

SURU is not yet C-independent. The current compiler, VM, standard library, and
build system are C17-based and require a host C compiler for normal builds.

## Required evidence

| area | required evidence | current state |
|---|---|---|
| compiler | complete compiler source in SURU | not started |
| runtime | SURU runtime and memory manager | not started |
| executable | standalone Linux x86-64 ELF64 output | not started |
| build | normal build invokes no C toolchain | not started |
| bootstrap | tested official bootstrap executable | not published |
| clean host | rebuild and application test without C tools | not run |

The C17 implementation remains an intentional Stage-0 reference. It must stay
working until all rows above have reproducible artifacts and passing tests.

## Audit method

The final clean-environment test will use a Linux image without GCC, Clang,
development headers, or libc development files. The official bootstrap binary
will build the compiler, runtime, native backend, and a normal application.
Build logs will be searched for compiler/linker invocations and the resulting
ELF dependencies will be inspected. The test will record the bootstrap binary
hash, source revision, host kernel/architecture, commands, exit codes, and
application output.

Until that record exists, release notes must not use “C-independent” or
“complete self-hosting” as a status claim.
