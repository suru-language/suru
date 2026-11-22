# SURU

SURU is an independent, statically typed, compiled general-purpose programming
language implemented from scratch in ISO C17. It has its own syntax, its own
bytecode, its own verifier, its own virtual machine, and its own runtime with a
custom managed heap and tracing garbage collector. The C17 implementation is
the current bootstrap and reference implementation; SURU self-hosting is the
next development phase.

## Highlights

- **Original implementation.** Lexer, parser, semantic engine, bytecode,
  verifier, VM, and runtime are all written from scratch. No reused source
  from another project.
- **Memory safe.** No raw pointers in the language; bounds-checked arrays;
  overflow-safe arithmetic; a custom tracing GC.
- **Verified bytecode.** The VM refuses to run a module that has not passed the
  bytecode verifier.
- **Portable.** The bytecode format is architecture-independent; Linux
  x86-64 is the bootstrapped target.

## Build

Requires a C17 compiler (GCC or Clang) and GNU make.

```
make            # debug build
make release    # optimized build
make asan       # AddressSanitizer build
make ubsan      # UndefinedBehaviorSanitizer build
make test       # run the full test suite
make selfhost-check  # check the transitional SURU lexer module
make selfhost-test   # differential tests for the SURU-written compiler/VM
suru --syntax=natural --compiler=selfhost compile hello.suru out.sbc
```

A CMake build is also provided (gcc/clang on Linux/macOS, and the MSVC
toolchain on Windows):

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
# AddressSanitizer + UndefinedBehaviorSanitizer build:
cmake -B build-asan -DSU_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan
```

See `docs/architecture.md` for details. The builtin contract is in
`docs/builtins.md`; the self-hosting status is in `docs/bootstrap.md` and
`docs/self-hosting.md`.

## Usage

```
suru run hello.suru [args...]   # compile and run (args via args()/args_len())
suru check hello.suru            # compile and type-check only
suru compile hello.suru out.sbc  # emit a bytecode module
suru exec out.sbc [args...]      # run a precompiled module
suru --version | --help
```

Banner `#!` lines are ignored, so `.suru` files can be made directly
executable. `use "mod.suru"` imports another file, resolved relative to the
current file and then via the `SURU_MOD_PATH` search path.

## Layout

```
docs/               specification, grammar, architecture, memory, security, roadmap
include/suru/       public interfaces
src/                implementation
tests/              unit, integration, fuzz
```

## Language features

Functions (first-class, higher-order, returning), **generics** (type-erased
`func f<T>`), records (positional and named-field construction), arrays and
strings (indexing, iteration, slicing, split/join), enums, compile-time
`const` expressions, closures with capture-by-value, `use` module includes,
and a CLI with program arguments. Operators cover arithmetic (with mixed
int/float promotion), comparison, logical, bitwise/shift, and a ternary
conditional. The bytecode optimizer performs dead-code elimination and
constant folding.

## Status

The C17 Stage-0 reference compiler and VM remain the complete default
toolchain. Natural Syntax 2.0 now has a SURU-written lexer, parser, semantic
passes, typed expression AST, and a partial bytecode compiler. The CLI exposes
`--syntax=natural --compiler=selfhost` to run that SURU-written compiler under
the Stage-0 VM; the self-hosted path is differentially tested on its supported
corpus but does not yet cover the full language. The SURU VM/runtime/GC port,
complete compiler parity, native backend completeness, and C-independent
self-rebuild remain in progress. See `docs/document-a-2.0.md`,
`docs/bootstrap.md`, and `docs/self-hosting.md` for status and acceptance
gates.
