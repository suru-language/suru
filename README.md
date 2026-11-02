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
suru run    hello.suru
suru check  bad_syntax.suru
suru compile hello.suru -o hello.sbc
```

## Layout

```
docs/               specification, grammar, architecture, memory, security, roadmap
include/suru/       public interfaces
src/                implementation
tests/              unit, integration, fuzz
```

## Status

Milestone 0.1.0: core toolchain implemented — independent lexer/parser,
semantic analysis, bytecode compiler, bytecode verifier, stack-based VM, custom
managed heap with tracing GC, standard library, CLI, and automated tests
(unit, integration, fuzz, ASan/UBSan, leak). See `docs/roadmap.md`.
