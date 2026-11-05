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
The source, token, diagnostics, and legacy lexer modules use the legacy grammar
for bootstrap compatibility. The Natural Syntax lexer follows approved
Document A 2.0 and is exercised through
`tests/selfhost/natural_lexer_driver.suru`. The C17 VM executes all transitional
modules.

`source.suru` owns file loading and line/column calculations, while
`diagnostics.suru` owns stable lexer diagnostic classification.
`token_stream.suru` exposes structured legacy tokens with source locations,
spans, decoded string values, and numeric values for the parser port. `make
selfhost-check` validates all frontend drivers, and `make selfhost-test`
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
next-phase work. The revised 2.0 design decisions (re-approval recorded in
`docs/document-a-2.0.md` and `docs/syntax-migration.md` on 2026-11-04) are
the binding contract for every remaining module: explicit matching block
terminators, `set ... to` as the only assignment statement, explicit public
routine types, `across`/`from`/`until`/`through` iteration, records,
contracts, choices, modules, interpolation, and typed exceptions. The first parser increment is
`selfhost/compiler/natural_parser.suru`, which validates block terminators,
routine context, `otherwise`/`case` ownership, and exception clauses against
the structural corpus. It now also validates binding initializers, explicit
routine parameter/public return types, module-scope constants, mutable
assignment targets, and routine `give` context.
The parser-facing cursor and precedence expression increment are in
`selfhost/compiler/natural_token_stream.suru` and
`selfhost/compiler/natural_expression.suru`; they preserve token spans and
serialize expression shapes for the AST port.
Core statement parsing and the first AST node record are in
`selfhost/compiler/natural_statement_parser.suru` and
`selfhost/compiler/natural_ast.suru`, currently covering `value`, `change`,
`constant`, `set ... to`, `show`, and `give`.
Control-flow AST parsing is now covered by
`selfhost/compiler/natural_control_parser.suru` for `when`, `otherwise`,
`repeat`, `each`, `choose`, `case`, and matching block terminators.
The first semantic pass is in `selfhost/compiler/natural_semantic.suru`; it
tracks bindings and types, infers local types, validates numeric/logical
expressions, checks mutable assignment targets, and requires Boolean control
conditions.
Declaration/effect validation is in
`selfhost/compiler/natural_declaration_semantic.suru`; its current gate checks
routine signatures and return paths, record fields and contract references,
choice payloads and duplicate variants, module aliases, and `attempt` handler/
`ensure` ownership.
Contract method signatures are compared against record implementations, choice
matches are checked for exhaustiveness or `_`, and
`selfhost/compiler/natural_module_semantic.suru` resolves relative imports and
rejects missing modules and import cycles.
`selfhost/compiler/natural_effect_semantic.suru` now checks calls to effectful
routines against declared `throws` effects and compatible `handle` clauses.
The bootstrap runtime effect model is in `selfhost/runtime/effects.suru`; it
defines normal/return/exception states, typed handler matching, nested unwind,
and cleanup replacement with retained causes.
The native GC now has `S_OBJ_EXCEPTION` with managed type, message, and cause
fields; its graph is marked and collected like other heap objects, and VM type
formatting identifies it as `exception`.
The bytecode/VM foundation now defines `BC_NEW_EXCEPTION`,
`BC_PUSH_HANDLER`, `BC_POP_HANDLER`, and `BC_THROW`; handler metadata is tied to
call depth and frame/stack restoration, with a caught-exception VM test.
`selfhost/compiler/natural_bytecode.suru` now lowers effect blocks into
`PUSH_HANDLER`, `POP_HANDLER`, handler labels, `JMP`, `RET`, and `RET_UNIT`
operations as the bridge to native instruction-index emission.
Its second pass removes symbolic labels and emits numeric instruction indices;
normal completion, handled exceptions, and `give` paths converge on the
cleanup label before the final return label.
The same emitter now lowers integer, Boolean, string, and `none` literals,
local bindings and assignment, arithmetic/comparison expressions, `show`, and
`give` to concrete constant-pool and VM instruction records. Handler variables
are stored in local slots before the handler body runs.
Builtin calls and calls to collected routine declarations are emitted as
`BC_BUILTIN` and `BC_CALL` records, respectively.
`repeat while`, `when`, `otherwise when`, and `otherwise` now emit concrete
`BC_JZ`/`BC_JMP` control flow with resolved numeric targets.

### Bytecode serialization, verification, and bootstrap targets (Milestone A)

The bytecode object-emitting half is now owned by SURU:

- `selfhost/compiler/natural_bytecode.suru` exposes `nbc_collect(source) ->
  [str]`; `natural_emit_bytecode` is a thin wrapper over it.
- `selfhost/compiler/natural_module_writer.suru` serializes the module to the
  `.sbc` wire format byte-for-byte identical to C `s_bc_write` (header, pools,
  record table, per-function slot remap, frame sizing, opcode table, branch
  target resolution, and `strtoul`/`strtod`-matching operand parsing).
- `selfhost/compiler/natural_verifier.suru` ports `src/verify.c` (`s_verify_module`):
  opcode/operand bounds, stack-depth BFS with merge-conflict detection, branch
  ranges, terminal reachability, and frame/depth limits.
- `src/vm.c`/`stdlib.h`/`stdlib.c` add an `f64_to_bytes` builtin so float pools
  serialize identically; `suru exec` now passes trailing argv to a module so a
  bootstrapped compiler can receive its source/output paths.
- Drivers: `natural_compile_driver.suru` (bytecode -> writer), 
  `natural_build_driver.suru` (frontend -> bytecode -> verifier -> writer), and
  `natural_verify_driver.suru` / `natural_verify_cases_driver.suru`.
- `selfhost/compiler/natural_module_reader.suru` parses the `.sbc` wire format
  back into pools/function tables and re-serializes byte-identically via
  `nw_emit_module`; it is the load half of a future SURU VM, gated by a
  write -> read -> re-write round-trip (`natural_reader_run.sh`, 57/57).
- `selfhost/compiler/natural_vm.suru` is a SURU-written interpreter that loads a
  `.sbc` module and executes a core of the instruction set (constants, locals,
  int/float arithmetic and comparison, string concat, control flow, function
  calls and recursion, arrays (literal construction, index read/write),
  records (construction, field read/write), exception handlers
  (`PUSH_HANDLER`/`POP_HANDLER`/`NEW_EXCEPTION`/`THROW`), print/println/to_str,
  returns) under the C17 VM, with output parity to the C VM as the gate
  (`natural_vm_run.sh`). A broad parity gate (`natural_vm_corpus_run.sh`)
  runs the interpreter against every fixture the C VM executes cleanly and
  requires identical output; it currently passes all 12 such fixtures.
  `natural_run_driver.suru` wires frontend -> bytecode -> verifier -> writer ->
  interpreter into one end-to-end SURU program. The frontend now resolves user
  routine calls in expressions (return types are recorded per routine and
  consulted by `nss_expr_type`), so recursive/multi-routine programs such as
  Fibonacci compile and run end-to-end and match the reference. The declaration
  pass also now accepts `give` inside `attempt`/`handle`/`ensure` blocks (the
  routine frame remains on the control stack beneath the effect frames), so
  exception programs run end-to-end as well. The full-pipeline
  drivers re-read the source after the frontend check because running the
  frontend first imposes GC pressure that corrupts aliasing source views (a
  known SURU string-aliasing fragility); re-reading yields deterministic,
  C-identical output. This is the first execution step of the Stage 2 VM port;
  closures (not produced by any frontend test) remain unimplemented.
- Gates wired into `make selfhost-test`: `natural_compile_run.sh` (differential
  byte-identity, 58/58), `natural_verify_run.sh` (positive + injected invalid),
  `natural_build_run.sh` (full-pipeline acceptance all byte-identical), and
  `natural_reader_run.sh` (module-reader round-trip).
- Make targets: `selfhost-compare` runs the three gates; `selfhost-bootstrap`
  compiles `natural_build_driver.suru` into `build/bin/natural_compiler.sbc`;
  `selfhost-rebuild` runs it on the natural corpus and requires byte-identical
  output to the C reassembly path.

The deferred self-rebuild identity gate (rebuilding the compiler twice yields
byte-identical bytecode) still depends on the compiler itself being written in
Natural syntax; the current build driver is a legacy host program that the
Natural frontend does not accept, so self-compilation of the driver is not yet
possible. That gate, together with the SURU VM/runtime/memory/GC port
(Stage 2 remainder) and the native ELF64 backend (Stages 3-4), remain open.
