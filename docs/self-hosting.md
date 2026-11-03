# Self-Hosting Plan

This is the implementation plan for moving from the C17 reference bootstrap to
a SURU-built toolchain. It records actual repository state as well as target
acceptance criteria; a target is not marked complete until its tests exist and
pass.

## Stages

1. **Stage 0:** the existing C17 compiler and VM remain the reference
   implementation.
2. **Stage 1:** a compiler written in SURU is compiled by Stage 0 and executed
   by the C17 VM.
3. **Stage 2:** the verifier, VM, runtime, and memory manager are ported to
   SURU and differentially tested against Stage 0.
4. **Stage 3:** the SURU compiler emits Linux x86-64 ELF64 without invoking a C
   compiler or linker.
5. **Stage 4:** the native toolchain rebuilds itself in a clean environment
   that has no C compiler, headers, or libc development dependency.

The C implementation remains the reference until the differential and
self-rebuild gates pass. No C component is removed as part of an intermediate
port.

## Proposed source layout

Self-hosted sources will be added under `selfhost/` only after the language
grammar is frozen. The first modules are expected to be:

```text
selfhost/compiler/  source, diagnostics, tokens, lexer, AST, parser, types,
                    symbols, semantic analysis, bytecode, verifier, CLI
selfhost/runtime/   values, objects, strings, collections, memory, GC, errors
selfhost/vm/        loader, verifier, stack, frames, interpreter, execution
selfhost/native/    x86-64 representation, instructions, calling convention,
                    code generation, ELF output
```

Every module in these directories must be SURU source. Platform assembly is
limited to documented Linux system interfaces.

## Reproducible bootstrap sequence

The build system will expose these commands after their inputs exist:

```text
make selfhost-check
make selfhost-bootstrap
make selfhost-test
make selfhost-compare
make selfhost-rebuild
```

The sequence is:

```text
C17 compiler -> Stage 1 compiler bytecode
Stage 1 compiler -> Stage 2 compiler bytecode
Stage 2 compiler -> Stage 3 compiler bytecode
```

Each stage compiles the same complete source tree. Comparisons cover bytecode,
diagnostics, exit status, output, module resolution, and runtime errors. Any
allowed metadata normalization must be documented before it is used.

## Acceptance gates

- Stage 1 compiles every compiler module and runs under the C17 VM.
- Rebuilding the compiler twice produces byte-identical bytecode, or a
  documented normalized equivalent when metadata is intentionally variable.
- The SURU VM passes the existing bytecode, verifier, runtime, and integration
  suites and agrees with the C17 VM on the differential corpus.
- Native output is a valid standalone ELF64 executable and does not invoke
  GCC, Clang, a C linker, or libc.
- A clean Linux environment can rebuild the compiler, runtime, native backend,
  and a normal application from the official bootstrap executable.

## Current status

The first transitional modules now exist at
`selfhost/compiler/source.suru`, `selfhost/compiler/tokens.suru`,
`selfhost/compiler/diagnostics.suru`, `selfhost/compiler/lexer.suru`, and
`selfhost/compiler/token_stream.suru`. The approved Natural Syntax lexer is in
`selfhost/compiler/natural_lexer.suru`.
They use the legacy grammar because Document A 2.0 is not yet frozen; they are
not a Natural Syntax implementation. The C17 VM executes them through
`tests/selfhost/lexer_driver.suru`.

`source.suru` owns file loading and line/column calculations, while
`diagnostics.suru` owns stable lexer diagnostic classification.
`token_stream.suru` exposes structured legacy tokens with source locations,
spans, decoded string values, and numeric values for the parser port. `make
selfhost-check` validates both frontend drivers, and `make selfhost-test`
compares the direct lexer and token stream for token kinds, source locations,
identifier text,
integer values, float values, escaped string values, and negative diagnostic
codes/locations against the C lexer. The current negative corpus covers
reserved identifiers, missing hexadecimal/binary digits, invalid escapes,
unterminated strings, and unexpected punctuation. Complete diagnostic message
parity and the remaining compiler modules are still open gates. The binary I/O
needed by a future SURU compiler is explicit in `include/suru/io.h` and
`src/io.c`. The Natural Syntax lexer gate also passes the approved corpus in
`tests/natural/`; the parser, semantic analyzer, and natural CLI mode remain
next-phase work.
