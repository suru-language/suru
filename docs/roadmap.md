# SURU Release Roadmap

Version scheme: `0.<minor>.<patch>` until 1.0.

## 0.1.0 (this milestone) — core toolchain

- [x] Language specification and formal grammar.
- [x] Lexer, parser, AST.
- [x] Semantic analysis (type inference, name resolution, record constructors).
- [x] Bytecode format and compiler.
- [x] Bytecode verifier.
- [x] Stack-based VM.
- [x] Managed heap and tracing GC (non-moving mark-sweep).
- [x] Standard library: print/println, string and array length, conversions,
      read_line, make_array.
- [x] CLI: `suru run`, `suru check`, `suru compile`.
- [x] Tests: unit, integration, fuzz, ASan/UBSan, leak.

## 0.2.0 — language completeness

- Closures with full capture and higher-order functions.
- `for` loops over arrays with bounds guarantees.
- Records with methods (via module-associated functions).
- Error reporting improvements: multi-line snippets, notes, suggestions.
- Integer and float formatting options in the standard library.

## 0.3.0 — performance and robustness

- [x] Bytecode optimizer passes: dead code elimination and constant folding.
- Nursery sizing heuristics; generational collection tuning.
- Incremental collection groundwork.
- Fuzz corpus expansion; coverage-guided fuzzing (libFuzzer/AFL).

## 0.4.0 — portability

- Windows support (MSVC-compatible build path, isolated OS layer).
- macOS support.
- Big-endian hosts validated against the fixed-endian bytecode format.

## 0.5.0 — ecosystem

- Package/module system (`use` for user modules).
- Standard library expansion: file I/O, formatted output, collections.
- Grammar and spec stabilization (freeze for 1.0).

## 1.0.0 — stable

- Frozen language and bytecode format (format version pinned).
- Backward-compatible evolution policy documented.
- Security assessment re-run; fuzzing corpus minimal release criteria.

## Release criteria for 1.0

- 1.0 language spec and grammar complete and reviewed.
- Full test suite green under GCC and Clang, debug/release/ASan/UBSan.
- No open memory-safety defects from ASan/UBSan or fuzzing.
- Bytecode verifier has 100% coverage of instruction set.
- Documented release process and versioning.
