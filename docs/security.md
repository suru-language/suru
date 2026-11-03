# SURU Security Assessment

This document is the security assessment of SURU 0.1.0. It covers the threat
model, the mechanisms that address each threat, and the known limitations.

## 1. Threat model

SURU's principal consumers are:

- **Programmers** compiling and running SURU programs.
- **Hosts** running untrusted SURU bytecode or untrusted source code in their
  environment.

We assume an adversary may provide arbitrary source text or arbitrary bytecode
(including hand-crafted bytecode that did not come from our compiler). The
adversary is *not* assumed to be able to modify the toolchain binary itself.

## 2. Areas of focus

### 2.1 Compiler frontend hardening

- The lexer and parser are **total**: they never read past the buffer and
  never loop without progress even on malformed input.
- All token and AST data lives in arenas; there are no per-node free paths to
  get wrong.
- Diagnostics are bounded; error recovery cannot recurse unboundedly because
  the parser caps nesting depth.
- Fuzz testing drives the frontend with arbitrary bytes (Section 6).

### 2.2 Verifier as the security boundary

The verifier is the gate between unverified bytecode and the VM. It enforces:

- Every jump target is an instruction boundary inside the same function.
- The operand stack type discipline is satisfied at every instruction.
- No stack underflow; the maximum stack depth is bounded by the declared frame.
- Instruction operands are in range with respect to the constant pool.
- Reads/writes of local slots are within the declared frame.

A module that fails verification is rejected. The VM asserts on load that
verification succeeded, and refuses to execute otherwise.

### 2.3 Memory safety

- No raw pointers in the language; the VM manipulates `Value` slots under
  bounds checks.
- Array indexing is bounds-checked at runtime and traps on OOB.
- The collector traverses only recorded child pointers; it cannot read
  uninitialized memory (see `docs/memory.md`).
- The overflow-safe arithmetic helpers are used in the compiler; traps are
  enforced at runtime for division by zero and, in debug builds, overflow.

### 2.4 Resource exhaustion

- Object size limits: array length and record sizes are bounded by 32-bit
  counts, and allocation is refused above a configured maximum.
- The VM caps recursion by bounding the call stack depth.
- Long-running loops are the program's own semantics; no artificial wall-clock
  limit is imposed.

## 3. Known limitations

- The runtime does not yet isolate between multiple modules; all code shares
  one heap. This is a roadmap item.
- There is no sandboxing of the standard library; builtins have full host
  access. Do not run untrusted programs in a privileged environment.
- While the bytecode format is architecture-independent, the VM is an
  interpreter; performance is not a security property.

## 4. Fuzzing

The fuzz targets in `tests/fuzz` are:

- `verify_fuzz` feeds arbitrary bytes to the bytecode verifier and asserts it
  either accepts or rejects cleanly, without crashing.
- `frontend_fuzz` feeds arbitrary bytes as source text through the full
  frontend — lexer, parser, semantic analysis, bytecode compilation, and a
  final verifier pass on the optimized output — and asserts it never crashes
  and reports errors cleanly without leaking.

Both targets are built under ASan/UBSan via `make fuzz-libfuzzer` (libFuzzer)
and as stdin-fed harnesses via `make fuzz`.

## 5. Build-time defense

- The default build enables `-fstack-protector-strong` and
  `-D_FORTIFY_SOURCE=2`.
- ASan and UBSan builds are supported and run the full test suite.
- The Makefile treats warnings as errors (`-Werror`) in CI configurations so
  that new code cannot be introduced with relaxed checking.

## 6. Security testing checklist

- [x] Fuzz harnesses run in CI (verifier, lexer, parser, VM).
- [x] ASan/UBSan builds pass the full suite.
- [x] Allocation-failure tests run.
- [x] Verifier rejects malformed modules.
- [ ] Cross-compilation / external triage (roadmap).
