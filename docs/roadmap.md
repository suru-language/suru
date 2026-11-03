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

- [x] Closures with full capture and higher-order functions.
- [x] `for` loops over arrays with bounds guarantees.
- Records with methods (via module-associated functions). Module-associated
  functions are supported; a `obj.method()` call syntax is not yet implemented.
- [x] Error reporting improvements: multi-line snippets, notes (secondary
  diagnostics with their own caret), and suggestions (did-you-mean) for
  misspelled names.
- [x] Integer and float formatting options in the standard library.

## 0.3.0 — performance and robustness

- [x] Bytecode optimizer passes: dead code elimination and constant folding.
- [x] Nursery sizing heuristics (adaptive live-bytes threshold with headroom
  sizing). Generational collection tuning remains.
- [x] Incremental collection: tri-color marking (white/grey/black) with a
  persistent grey worklist and a write barrier at array/record stores that
  shades white targets written into black objects. `SURU_GC_INCREMENTAL=1`
  enables incremental collection: marking runs in slices interleaved with
  allocation (budgeted `s_heap_collect_step`), new objects are shaded grey
  during a cycle, roots are re-seeded per slice, and the sweep runs when
  marking completes. Default (unset) is the full stop-the-world collect;
  `make integration-incremental` runs the suite in incremental mode.
- [x] Fuzz corpus expansion; coverage-guided fuzzing (libFuzzer via
  `make fuzz-libfuzzer`). Fuzzing found and fixed an uninitialized-read
  free in the bytecode deserializer's failure path.

## 0.4.0 — portability

- Windows support (MSVC-compatible build path, isolated OS layer).
  Compile-time MSVC blockers (builtins, `__builtin_unreachable`,
  `__attribute__`) are resolved; the OS-layer/build integration is deferred.
- [x] macOS support. Toolchain validated under Clang (debug, ASan/UBSan,
  and CMake); the portable CMake build targets MSVC/macOS.
- Big-endian hosts validated against the fixed-endian bytecode format.
  Format/code are explicit little-endian with a pinning test
  (`test_le_endian`); no big-endian cross-toolchain is available to run a
  true big-endian validation here.

## 0.5.0 — ecosystem

- [x] Package/module system (`use` for user modules; resolved via
  `SURU_MOD_PATH`).
- [x] Standard library expansion: file I/O (read_file/write_file/file_exists),
  formatted output (int_to_str_base/str_pad), collections
  (array_concat/array_contains/array_index_of/array_sort). `int_from_str`
  documented and made strict (rejects trailing garbage).
- [x] Build-time hardening: default builds (Makefile and CMake) now enable
  `-fstack-protector-strong` and `-D_FORTIFY_SOURCE=2`; security assessment
  corrected to match the actual fuzz targets (frontend + verifier).
- [x] Grammar and spec stabilization (freeze for 1.0): reconciled the
  keyword list across grammar/spec/implementation (`enum`, `do`, `and`,
  `or`, `not`); grammar now includes `do_stmt`; the spec's reserved `__`
  identifier rule is now enforced by the lexer (error 111). Found and fixed a
  latent bytecode bug: the `or` operator generated unbalanced stack depths
  (true-branch dropped its operand), so every `or` expression failed to
  compile; it now mirrors `and` with a consistent single-value result on both
  paths, and short-circuit behavior is covered by a new `logical` test.
  Added a missing-return check: a function with a non-unit return type must
  have a `return` on every control path (error 462), replacing a silent
  runtime trap; spec section 6 corrected to state that a value is produced
  only by an explicit `return` (no trailing-expression return). Found and
  fixed a `for`-loop bug: `continue` jumped to the condition check instead of
  the iterator increment, so `continue` in a `for` loop never advanced the
  index and hung forever; it now targets the increment step (covered by a new
  `forcont` test). Found and fixed broken compound assignment on indexed
  targets: `BC_IDX_LD` pops `(idx, obj)` and `BC_IDX_ST` pops
  `(val, idx, obj)`, but the compiler emitted the store operands in the wrong
  order, so `a[i] += x` (and the captured-variable form `n += x` inside
  closures) failed at runtime with an "index store/type error". Both paths
  now stage obj/index/result in temp frame slots (covered by a new `cmpassign`
  test). Fixed two string-escape defects: `\u00XX` escapes (code points below
  0x80) consumed the closing quote and were mis-reported as unterminated
  strings, and lone surrogate code points (`\uD800`-`\uDFFF`) were encoded as
  invalid UTF-8 instead of being rejected. Both are corrected; surrogate
  escapes are now a compile error (covered by `escapes`/`badsur` tests).

## 1.0.0 — stable

- Frozen language and bytecode format (format version pinned).
- Backward-compatible evolution policy documented.
- Security assessment re-run; fuzzing corpus minimal release criteria.

## Release criteria for 1.0

- 1.0 language spec and grammar complete and reviewed.
- Full test suite green under GCC and Clang, debug/release/ASan/UBSan.
- No open memory-safety defects from ASan/UBSan or fuzzing.
- Bytecode verifier has 100% coverage of instruction set (confirmed: every one
  of the 70 opcodes has an entry in `s_bc_op_table` with pops/pushes/branch/
  terminal metadata, plus operand-specific checks and stack-depth analysis).
- Documented release process and versioning.
