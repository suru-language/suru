# SURU

SURU is an independent, statically typed, compiled general-purpose programming
language implemented from scratch in ISO C17. It has its own syntax, its own
bytecode, its own verifier, its own virtual machine, and its own runtime with a
custom managed heap and tracing garbage collector.

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
```

See `docs/architecture.md` for details.

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

All 0.1.0 through 0.3.0 roadmap items are implemented and tested: independent
lexer/parser, semantic analysis (including generics), bytecode compiler with
optimizer passes, bytecode verifier, stack-based VM, custom managed heap with
tracing GC (tunable threshold), standard library, CLI, and automated tests
(unit, integration, fuzz, ASan/UBSan, leak). Remaining roadmap items are host
portability (Windows/macOS/big-endian) and a fuller package system. See
`docs/roadmap.md`.
