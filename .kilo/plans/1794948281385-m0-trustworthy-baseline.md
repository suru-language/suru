# M0 — Trustworthy Baseline (resolved execution plan)

**Repository:** `suru-language/suru`  
**Base checkpoint:** `main` at `b0fec6d359694d088dae677be92272f762c917b5`  
**Working branch:** `m0-trustworthy-baseline`  
**Required path:** `.kilo/plans/1794948281385-m0-trustworthy-baseline.md`

## Objective

Establish a reproducible Stage 0 baseline, clarify source/runtime ownership, normalize per-function compiler slots/types/frames/labels, migrate the legacy corpus to exact JSON manifests, and preserve bytecode v1.

## Invariants

- Bytecode magic is `SURU`; serialized version remains `1`.
- No serialized field, opcode value, operand width or module-table ordering changes.
- Token/AST references must not outlive the source bytes they point into.
- Each function has its own slot/type/frame metadata; all branch labels resolve before verification.
- Corpus stdout is byte-exact when a golden exists; unsupported stdout assertions state why.
- Tests are reported passed only after actual scratch-clone execution.

## Execution order

### 1. Checkpoint
Save this file at the exact path, clone the branch in an isolated scratch directory, verify the plan/base revision/branch HEAD in that clone, and record final worktree state.

### 2. Stage 0 and specifications
Record the historical Stage 0 measurements separately from current test results. Document bytecode v1, all 73 opcode IDs and metadata, all 57 builtin IDs/names/signatures/behavior, CLI/include/verifier/VM/GC/Natural/self-host/native paths, and unit/integration/Natural/self-host/fuzz coverage.

### 3. Source ownership and GC
Remove source content re-reads: metadata-only include probing followed by a single source read. Reuse the bounded byte allocation for the source terminator. Make identifier and decoded string token bytes arena-owned. Add token/include lifetime tests and prove managed-string liveness with allocation interval one.

### 4. Per-function normalization
Record parameter/local slot types and semantic frame size per function. Use semantic slot allocation in codegen instead of a second AST traversal. Keep label state function-local and reject unresolved patches. Do not serialize compiler-only metadata.

### 5. Corpus migration
Use Python 3 standard-library tooling and JSON manifests. Record source, arguments, fixtures, environment, exit status and exact stdout golden. Explicitly label missing stdout goldens unsupported. Keep generated syntax and self-host tests in their separate runner.

### 6. Exact I/O
Use one exact-size `fread`, reject short reads, preserve failure output-parameter semantics and the existing short-write retry path, and cover empty/binary files.

### 7. Verification gates
Run normal tests, incremental GC, allocation-interval-one GC, sanitizers, analysis, determinism, Clang build/tests, and final scratch-clone worktree checks. Record command, exit status and output. Any gate not executed successfully remains blocked.

## Completion rule

M0 completes only when the documentation/inventories are present, bytecode v1 is unchanged, lifetime/compiler invariants hold, every corpus entry is golden or explicitly stdout-unsupported, and every required scratch-clone gate passes.

## Execution record

**Scratch-clone workflow:** [run 44991325117](https://github.com/suru-language/suru/actions/runs/44991325117)  
**Verified code revision:** `1094837c96c24dcdfcd9a563d2378d4b59481e5b`  
**Outcome:** all workflow steps passed.

| Gate | Executed command / condition | Result |
|---|---|---|
| File checkpoint | Fresh shallow clone of `m0-trustworthy-baseline`; verified this plan, architecture document and manifest; recorded `git rev-parse HEAD` | PASS |
| Normal build/tests | `make clean && make test` | PASS — 157 unit checks; integration 94 tests / 0 failures; self-host and Natural suites passed |
| Bytecode determinism | `make determinism-test` | PASS — independently compiled bytecode files compared identical |
| Incremental/aggressive GC | `SURU_GC_INCREMENTAL=1 make integration`; manifest case sets `SURU_GC_ALLOC_INTERVAL=1` | PASS — integration 94 tests / 0 failures, including retained `read_file` string across managed allocations |
| Static analysis | `make analyze` | PASS — Clang analyzer completed for all `LIBSRC` files |
| Sanitizers | `make BUILD=build-asan asan-test` | PASS — ASan/UBSan test suite |
| Clang | `make CC=clang BUILD=build-clang test` | PASS — unit, integration and self-host suites; bootstrapped compiler output matched C output byte-for-byte |
| Final worktree | `git diff --exit-code`; `test -z "$(git status --porcelain)"` in the scratch clone | PASS — clean |

The successful run is on `1094837c96c24dcdfcd9a563d2378d4b59481e5b`. The bytecode version declaration remains `S_BC_VERSION 1u`; the M0 changes do not modify the bytecode serialization definitions. The initial failed workflow attempts were resolved before this successful run: the GC golden was corrected to the observed boolean representation (`1\n1\n`), the determinism target stopped relying on Make-escaped shell variables, and two Natural-driver `snprintf` warnings were fixed with input validation and bounded function-name handling.

