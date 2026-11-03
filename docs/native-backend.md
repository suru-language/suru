# Native Backend Design Record

The native backend is a later stage of self-hosting. This file prevents the
project from implying that the current bytecode compiler is already native.

## Target and output

The first target is Linux x86-64. The output format is ELF64 produced by
SURU-controlled serialization. The backend must not call GCC, Clang, `ld`, a C
compiler, or a C runtime as an implicit code-generation step.

## Design decisions to freeze before implementation

The native design must specify, with conformance tests:

- the in-memory representation of every SURU value and managed object;
- integer, floating-point, string, array, map, record, closure, `Result`, and
  `Option` layouts;
- register and stack allocation, function prologues/epilogues, and the Linux
  x86-64 calling convention;
- generated checks for indexing, arithmetic traps, allocation failure, and
  runtime error propagation;
- closure environments, garbage-collector roots, and write barriers;
- direct Linux system-call interfaces and the minimal assembly boundary;
- ELF headers, program headers, load permissions, entry point, and relocation
  policy.

No design choice is silently inherited from C ABI behavior. The native backend
will initially lower the same verified intermediate representation used by the
bytecode backend, so invalid source or invalid compiler state cannot reach ELF
emission.

## Implementation order

1. deterministic native value representation and arithmetic;
2. straight-line functions and branches;
3. calls, stack frames, and runtime error returns;
4. strings and managed allocation;
5. closures and collections;
6. direct ELF64 output and system interfaces;
7. compiler self-rebuild and native differential tests.

There is no native backend in the current audited revision.
