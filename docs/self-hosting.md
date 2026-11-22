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
  C-identical output.   This is the first execution step of the Stage 2 VM port. The interpreter now
  implements the full instruction set, including closures (`BC_MKCLOSURE`,
  `BC_MKCLOSURE_V`, `BC_CALLV`) matching the C VM's capture/call convention
  (env placed in callee local slot 0, args after it); a hand-built module gate
  (`natural_vm_closure_run.sh`) verifies identical output on both VMs.
  Closures are not yet produced by the Natural frontend, so they are exercised
  only through that crafted-module gate. The interpreter is also reachable from
  the CLI as `suru exec --vm=natural`, which runs a module through the SURU VM
  and is gated to match the default C17 exec on the clean corpus
  (`natural_exec_vm_run.sh`).
- `selfhost/compiler/natural_elf_writer.suru` is a Stage 3 seed: a SURU-written
  ELF64 emitter that produces a minimal static x86-64 executable (write + exit
  syscalls, no C compiler/linker). It is gated by `natural_elf_run.sh`, which
  verifies `file` recognizes the output as an x86-64 ELF64 executable and that
  it runs and prints "hi". A minimal native-backend path
  (`natural_elf_compile_driver.suru`) compiles a trivial `show(x)` program
  end-to-end in SURU into a standalone x86-64 ELF64 executable that prints the
  literal, a non-negative integer constant, an integer expression over
  const/neg/add/sub/mul/div, or a program with integer local variables
  (`ld_local`/`st_local` over a stack frame with a red zone so the print
  routine's scratch cannot overlap locals; precedence is already baked into the
  bytecode; values are printed via raw x86-64 decimal conversion)
  (`natural_elf_compile_run.sh`). A micro-assembler (`nw_emit_elf_repeat`)
  emits a `repeat while` loop with a counter, comparison (`setl`/`test`), and
  resolved `jz`/`jmp` rel32 targets; it prints "012" and is gated by
  `natural_elf_loop_run.sh`. A generic bytecode->x86-64 compiler
  (`nw_emit_elf_program`) now lowers constants, locals, all integer comparisons
  (eq/ne/lt/le/gt/ge), arithmetic, `jmp`/`jz`/`jnz`, `println`, `pop`, and
  returns, so `when`/`otherwise` branches also compile to standalone executables
  (`natural_elf_when_run.sh`). A multi-function emitter
  (`nw_emit_elf_multifunc`) compiles every routine with its own frame and
  supports `BC_CALL` (call rel32, result returned in `rax`) with the ELF entry
  set to `main`, so no-argument helper routines compile and run
  (`natural_elf_call_run.sh`). Native argument passing (up to two integer args
  via `rdi`/`rsi`, spilled into callee frame slots) and recursion now work, and
  the integer `to_str` builtin is a no-op (println prints ints directly)
  (`natural_elf_call_run.sh` includes a 2-arg call and recursive `fib(6)`=8).
  Native integer array literals + index reads and records (construction, field
  write/read) now work via a single-segment writable static data buffer with a
  runtime bump allocator (`NEW_ARRAY`/`NEW_RECORD` allocate at a fixup-resolved
  data vaddr, `IDX_LD`/`FIELD_LD`/`FIELD_ST` access it), gated by
  `natural_elf_array_run.sh` and `natural_elf_record_run.sh`. Native calls
  support up to six integer arguments via the SysV registers (`rdi,rsi,rdx,
  rcx,r8,r9`), spilled into the callee frame slots (`natural_elf_call_run.sh`).
  The native backend is also reachable from the CLI as `suru --syntax=natural
  --backend=native compile|run` (shells out to the SURU native compiler driver),
  gated by `natural_exec_native_run.sh`. This is the first control-flow, call,
  and array/record step of the Stage 3 backend; the SURU-managed GC/memory port
  and   the C-independent self-rebuild remain.   A Stage 4 seed now exists:
  `tests/natural/selfhost_tokenizer.suru` is a compiler-like component (a
  character classifier / tokenizer) written in **Natural syntax** and making
  use of `str_len`/`substr`/`str_contains`/`str_compare`. To support it the
  frontend gained a builtin signature table (`nss_builtin_type`), the bytecode
  emitter maps those builtins to their ids, and the SURU VM implements
  `str_len`/`substr`/`str_contains`. The program passes the unified frontend,
  is accepted by the full build pipeline (writer/build/verifier/reader gates
  all include it), and now **executes** on the default C path, emitting the
  expected comma-separated token stream (`natural_tokenizer_run.sh`). Local
  variable names in the seed were initially unique across routines to avoid a
  global slot namespace limitation; Stage 1 now scopes slot/type markers by
  routine and adds a regression for repeated parameter names. The SURU VM was extended with the
  string/boolean builtins this program uses (`str_len`, `substr`,
  `str_contains`, `bit_and`, `bit_or`, `eq_s`), and the tokenizer now also
  **executes on the SURU VM** matching the C path; it is included in the VM
  corpus-parity gate (13/0). The frontend also gained array-builtin typing
  (`array_concat`/`slice`/`range` return the first argument's array type;
  `range`/`make_array` return `[Whole]`), regression-locked by the frontend
  gate; this unblocks array-based Natural programs, a prerequisite for a
  SURU-managed heap/GC. (Array indexing typing was attempted but regressed a
  fixture and was dropped.) Executing it on the SURU VM is still unstable due to
  the host GC aliasing bug, which remains the main blocker for running a
  compiler written in Natural end-to-end. A root cause for one execution
  failure was traced to the legacy bytecode emitter's global slot namespace:
  a name reused in another routine could receive a slot ID that violated that
  routine's parameter ordering. Stage 1 now scopes `#slot` and `#type`
  markers by the active routine. The module writer continues to compact slots
  independently per function, and `tests/natural/per_function_slots.suru`
  verifies that a parameter reused at a different position in another routine
  preserves correct argument order. The bootstrap/rebuild loop
  (`selfhost-bootstrap` builds the SURU-written compiler driver into
  `build/bin/natural_compiler.sbc`; `selfhost-rebuild` runs it and requires
  output byte-identical to the C reassembly path) is now part of `make test`. (A native array attempt required a second loadable segment, which
  intermittently triggered a host C17 VM memory-corruption bug; it was reverted
  to keep the suite green. Arrays/records in native output therefore remain
  open pending a more robust code layout.)
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
Natural syntax; the current build driver is a legacy-syntax SURU program that
the Natural frontend does not accept, so self-compilation of the driver is not
yet possible.

### Stage 1: public self-hosted compiler entry point

The C17 CLI now exposes the SURU-written Stage 1 bytecode compiler explicitly:

```text
suru --syntax=natural --compiler=selfhost check source.suru
suru --syntax=natural --compiler=selfhost compile source.suru output.sbc
suru --syntax=natural --compiler=selfhost run source.suru
```

This route runs `tests/selfhost/natural_build_driver.suru` under the Stage 0
C17 VM. That SURU driver performs the Natural frontend checks, emits bytecode,
runs the SURU-written verifier, and serializes the module with the SURU module
writer. The default Natural CLI route remains the C reference implementation
until the self-hosted route reaches complete language and diagnostic parity.
The native backend is not yet part of this flag; it remains a separate Stage 3
path.

Expression lowering in `natural_bytecode.suru` now parses each expression
through `natural_expression_ast.suru` and recursively lowers the resulting
atom/unary/binary/conditional/array/index/field/call node graph. The statement
collector still identifies statement boundaries from source lines; replacing
that remaining source-shaped collection with the typed module AST is the next
compiler increment. `tests/natural/typed_expression_lowering.suru` exercises
operator precedence, ternary selection, arrays/indexing, records/fields, string
concatenation, builtins, and exact bytecode parity against the reference
compiler.


The C17 bytecode compiler now publishes every function's name and
parameter count before verifying individual function bodies. This is required
for forward calls: the verifier uses the callee's parameter count to compute
the caller's stack effect even when the callee appears later in source order.
`tests/integration/forward_call_late_declaration.suru` keeps a three-argument
forward call inside a loop so a missing signature prepass is caught as a
branch-stack merge mismatch.

The Stage 1 CLI gate checks byte-identical output against the reference
compiler, check/run behavior, module execution, and paths containing spaces.
The corpus-wide build parity gate now exercises the public
`--compiler=selfhost` interface instead of invoking the driver directly.

The exact-revision Stage 1 workflow passed all gates on
`cfa5e7faa41d342051986536fba482c1ca6a9e53`
([run 45011111835](https://github.com/suru-language/suru/actions/runs/45011111835)).
The self-hosted bytecode builder currently accepts **19** of the **59** Natural
fixtures that the Stage-0 compiler accepts; the remaining **40** are reported
explicitly as unsupported by `natural_build_run.sh`. Five additional Natural
fixtures are rejected by the Stage-0 reference and are not counted as supported
compiler inputs. This is a measured partial-compiler milestone, not full
language parity or C independence.



The first Stage 1 slot-isolation increment is implemented on the
`stage1-per-function-slots` branch. Compiler slot and type markers are scoped
by the active routine, keeping parameter slots ordered from zero within each
function. The regression `tests/natural/per_function_slots.suru` reuses
`shared` with different types and parameter positions in two routines, and gives
the second routine an unused leading parameter. This checks slot/type isolation
and preservation of fixed parameter positions during slot compaction. The build gate compares
SURU-writer bytecode with the reference and executes the result.
The full SURU VM, managed runtime/GC, native backend completion, and
C-independent self-rebuild remain later acceptance gates.

### Stage 2: SURU-managed heap foundation

`selfhost/runtime/heap.suru` begins the managed-memory port in SURU. It
represents heap objects using stable integer handles and a flat object table,
with a kind, payload, and child-reference list for strings, arrays, records,
closures, and exceptions. Its mark/sweep pass seeds roots and traverses the object graph
iteratively, safely collects unreachable cycles, and reuses reclaimed handle
slots. The incremental API advances a bounded number of grey objects per
slice, re-seeds roots on each slice, shades new allocations during a mark
cycle, and applies a tri-colour write barrier when a black object receives a
white child. Allocation slots now retain a logical reservation size: small
objects use ascending 16-byte classes through 4096 bytes, and larger slots use
first-fit reuse when their capacity is sufficient. Sweeping retains the
capacity class, while payload/reference growth raises the reservation without
changing the stable handle. Live/reserved object and byte snapshots, including free-slot counts, are
exposed for tests and threshold tuning; byte values are logical SURU table
estimates, not measurements of host memory. The incremental state also tracks
allocations since the last completed cycle, completed cycle count, cumulative
logical allocation bytes, the current threshold, and the cycle's before-live
snapshot. The heap module implements and tests the C VM's 512-allocation /
1 MiB live-byte trigger and four-times-survivor threshold adaptation, including
minimum/maximum clamps and the high-survival anti-thrashing rule. The
SURU-written VM currently advances a bounded 128-object mark slice at every
managed allocation; wiring the C-compatible interval/threshold trigger into
the VM remains gated on complete runtime safepoint and root coverage. This
conservative schedule is the version validated against the full VM corpus.
The regression driver exercises shared references, unreachable cycles,
exception causes, dropping roots, full reclamation, class-aware slot reuse,
large first-fit reuse, capacity growth, incremental root discovery, allocation
shading, write barriers, and trigger/threshold policy.

The SURU-written interpreter in `natural_vm.suru` now stores text
constants/results, bytecode arrays, records, and typed exceptions as managed
heap handles. String concatenation, comparisons, printing/conversion, length,
substring, and contains operations resolve managed strings through the heap.
Array/field reads and writes dereference handles; nested object references are
recorded in the object graph, and shared aliases observe mutations through the
same handle. Exception objects retain managed type and message strings and a
managed cause link; typed handler dispatch matches the exception type name.
Closure values are also managed objects. A closure with captures owns a
managed environment array, and the closure records that array as a child.
Calling the closure installs the environment handle in the new call frame, so
captured handles remain visible to root scanning after the closure value leaves
the operand stack. A C-vs-SURU VM regression captures a managed string and reads
it inside the called routine; the existing closure parity fixture remains
intact.

The interpreter advances a bounded mark slice (budget 128 objects) on
each managed allocation, re-seeding roots from its operand stack and active
call-frame locals. New objects are shaded grey during an active cycle, and
mutable array/record writes use the tri-colour barrier when a black object gains
a white child. When the grey queue drains, the VM sweeps and seeds the next
cycle. The large self-hosted tokenizer VM corpus exercises repeated string
allocation under this scheduler; the interval-one GC gate also covers the
interpreter's host allocations.

This remains a Stage 2 port, not a replacement for `src/mem.c` or the default
C VM: the SURU interpreter itself still runs under the Stage 0 C17 VM. Porting
allocation accounting/reuse policy parity and making the native/default runtime
independent of C remain open work.
The next Stage 1 AST increment adds `NAstModule`, a typed collection of
`NAstNode` records with parse-error metadata. The statement parser's serialized
nodes are rehydrated into the module array. The module driver is compared
byte-for-byte with the existing statement and control AST drivers for bindings,
assignments, show/give, routine-expression initializers, `when`/`otherwise`,
`repeat`, `each`, `choose`/`case`, and invalid statement/control diagnostics.
Expression payloads are still source-shaped text at this point. The next
compiler step is to replace those payloads with typed expression children and
make bytecode lowering consume the AST instead of rescanning source lines.
A typed expression AST increment adds `NAstExpr` nodes in a flat indexed table.
The new Pratt parser records atom, unary, binary, array, call, field, index,
routine-expression, and conditional nodes with child indices. The initial
conformance gate checks operator precedence and logical/comparison tree shape
against the existing expression parser. The expression tree is not yet attached
to statement nodes or consumed by bytecode lowering; that is the next step.


### Stage 1: typed expression attachment

The module AST now carries one flat `NAstExpr` table. Each `NAstNode` stores
zero or more root indices into that table. The module collector parses and
attaches expression roots for binding initializers, assignment targets and
values, `show`, `give`, `when`, `repeat while`, `each across/from/until/through`,
`choose`, `case`, and `otherwise when`. The new typed-AST gate checks
operator precedence, child indices, and statement-to-expression root links.
The original source-shaped node payload remains available for diagnostics and
differential comparison. The next increment is to lower this AST directly to
bytecode and compare it against the existing source-shaped emitter.

## Stage 1 typed expression analysis

The module AST currently attaches flat expression-node triplets. The next Stage 1 increment adds `natural_expression_types.suru`, a SURU-written recursive type checker over those AST node IDs. It handles primitive literals, local bindings, unary and binary operators, conditionals, homogeneous arrays, indexing, record-field lookups supplied by the enclosing module, and a core set of builtin signatures. It returns structured `ok/type` or `error/message` results without reparsing expression source text.

The focused test driver supplies a small module environment (locals, an array, and a record field table) and exercises valid and invalid expressions. This is an incremental typed-AST contract; complete routine signature collection, lexical scope lifetimes, generic inference, closure types, and direct AST-to-bytecode lowering remain subsequent Stage 1 tasks.

## Stage 1 module semantic attachment

The expression type checker is now being integrated with the module AST for a first statement-level semantic slice. The module pass tracks bindings in source order, checks declared initializer types, checks assignment target/value compatibility, requires Boolean control conditions, and validates array/text iteration sources and integer range bounds. It uses the attached expression-node IDs and type checker; it does not parse expression text a second time.

This initial module pass deliberately covers straight-line/module-level bindings and the supported control nodes. Lexical block scopes, mutability enforcement, routine parameter/return signatures, record declarations/methods, generic substitution, and effect typing remain separate follow-up work.

## Stage 1 routine declarations in the module AST

The SURU-written module AST parser now recognizes top-level routine declarations and records their name, typed parameter list, return type (defaulting to `Unit`), and declared `throws` names. Routine bodies are collected as the same ordered statement/control nodes used by the expression attachment pass, and `end routine` is checked against the open-block stack. This establishes routine boundaries and signatures in the SURU AST before the next step adds routine-local scopes and connects those AST nodes to bytecode lowering.

## Stage 1 AST-driven bytecode lowering

The AST-driven bytecode collector in `natural_ast_bytecode.suru` consumes routine/module AST nodes and attached expression-node IDs directly. Its executable subset now includes typed routine parameters and returns, local bindings and assignments, `show` and `give`, nested `when`/`otherwise`, `repeat while`, and integer range loops. The range-loop lowering evaluates the start and upper bound once, keeps the iterator and bound in function-local slots, uses an exclusive comparison for `until` and an inclusive comparison for `through`, and resolves loop branches through the SURU module writer. The emitted instruction records go through the existing SURU verifier and writer without changing bytecode v1.

The differential test still compares the byte-identical two-routine subset supported by both collectors. A separate AST-only execution fixture checks `each ... from ... until` and `each ... from ... through`, because the older source-scanning reference collector does not yet lower these new range constructs. The source-scanning collector remains the reference for records, choices, exceptions, and wider language coverage; remaining AST lowering and removal of source-line collection from the normal Stage 1 compiler path are still open tasks.


### Stage 2 root-coverage regression: suspended caller frames

tests/selfhost/natural_vm_local_roots_mk_driver.suru builds a two-function
bytecode module. main stores a managed string in local slot 0, calls a
separate churn routine, and reads the string only after that routine
returns. churn performs 520 managed string allocations, discarding each
temporary value. This forces the SURU VM's incremental collector to make
progress while the only live reference to the retained string is in a
suspended caller frame.

The regression runs the same module through the C reference VM and the
SURU-written VM and requires identical output (caller-root). It specifically
guards the VM root-set contract: every active call frame's locals, not merely
the top frame's locals and the operand stack, must be scanned at allocation
safepoints. The runner is included in both make selfhost-test and
make selfhost-compare.

This fixture intentionally validates root reachability under the already-validated
per-allocation mark-slice scheduler; it does not claim that the SURU VM has
adopted the production C VM's 512-allocation / live-byte collection trigger.
Keeping those acceptance questions separate makes a failure attributable to
root coverage rather than a change in collection scheduling.
