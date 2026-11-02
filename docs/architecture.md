# SURU Architecture

This document describes the implementation architecture of the SURU toolchain.

## 1. Pipeline

```
 Source
   |
   v
 Lexer  ──►  Token stream
   |
   v
 Parser ──►  AST
   |
   v
 Semantic analysis ──►  Typed AST + resolved names
   |
   v
 Bytecode compiler ──►  SURU bytecode module
   |
   v
 Bytecode verifier ──►  Verified module
   |
   v
 SURU VM (interpreter) ──►  Runtime + standard library
```

The compiler frontend (lexer → bytecode compiler) is a separate program from
the runtime/VM. The VM accepts *only* verified bytecode. The verifier runs
before any code executes, and the VM refuses to run an unverified module.

## 2. Source layout

```
include/suru/      public interfaces (one header per subsystem)
src/               implementation
tests/unit/        unit tests (run on the host)
tests/integration/ end-to-end tests (compile + run SURU programs)
tests/fuzz/        fuzz harness
tools/             auxiliary scripts
docs/              specification and design documents
```

## 3. Subsystems

### 3.1 Base utilities (`base.h`, `base.c`)

Common definitions: integer types, `SU_ASSERT`, diagnostics plumbing, and
bounds/overflow-safe arithmetic helpers. Every piece of code uses these
helpers for signed arithmetic so that wraparound is never relied upon.

### 3.2 Lexer (`lexer.h`, `lexer.c`)

Converts UTF-8 source text into a token stream. Produces tokens for literals,
identifiers, keywords, operators, and punctuation. Tracks line/column for
diagnostics. All string data is copied into a compiler arena, so tokens do not
own source buffer memory.

### 3.3 Parser (`parser.h`, `parser.c`)

Recursive-descent parser implementing the grammar in `docs/grammar.md`.
Produces an AST allocated in a compiler arena. On error it records a
diagnostic and attempts recovery to report multiple errors.

### 3.4 AST (`ast.h`)

The syntax tree. Nodes are allocated in an arena; ownership is per-arena, and
the whole AST is freed when compilation of a module finishes.

### 3.5 Semantic analysis (`sema.h`, `sema.c`)

Resolves names, infers and checks types, and rejects ill-formed programs. It
produces a typed AST with resolved symbol references. It also computes for
each function the maximum operand-stack depth required, which the bytecode
compiler uses for stack allocation.

### 3.6 Bytecode format (`bc.h`)

`bc.h` defines the SURU instruction set and the serialized module format. The
format is architecture-independent: integers are stored in a fixed, explicit
byte order (little-endian) with explicit widths, and no host type ever leaks
into the format. See `docs/bytecode.md` (section 7 below).

### 3.7 Bytecode compiler (`bcc.c`)

Lowers the typed AST into an execution's bytecode module. Emits constants,
functions, and instructions. The output is always structurally valid; the
verifier is the authority on module validity.

### 3.8 Bytecode verifier (`verify.c`)

A static checker over the serialized module. It walks the function body,
maintains a symbolic operand stack of types for each instruction, and rejects:
- type mismatches on the operand stack,
- unterminated branches and fallthrough past the end,
- jumps to non-instruction offsets,
- reads that pop more values than the stack holds,
- `dup`/`swap` on empty stacks.

The VM loads a module only after verification succeeds.

### 3.9 Virtual machine (`vm.c`)

A stack-based interpreter. It executes verified bytecode against runtime
values. The VM owns the runtime stack and the managed heap. It never
allocates after a GC is triggered except through the managed allocator.

### 3.10 Runtime (`runtime.c`, `value.c`)

Runtime values and operations. The runtime is shared by the VM and the
standard library and does not depend on the compiler frontend headers.

### 3.11 Managed heap + GC (`mem.h`, `mem.c`, `gc.c`)

See `docs/memory.md`.

## 4. Ownership and lifetime rules

- Every heap allocation is owned by an arena or the managed heap; there is no
  ad-hoc manual `malloc`/`free` pairing in compiler code.
- Arena allocations are freed in bulk at the end of a compile.
- GC objects are owned by the managed heap and freed by the collector.
- The VM stack frames reference GC objects only through `Value` slots that the
  collector treats as roots.

## 5. Portability

- The toolchain builds with GCC and Clang under strict warnings.
- Endianness and host `sizeof` never affect the serialized bytecode format.
- OS-specific code is confined to a small portability layer (e.g. `argv`
  handling, open/read/write), isolated in `runtime.c` and `driver.c`.

## 6. Testing strategy

- Unit tests exercise individual subsystems on the host.
- Integration tests compile and run SURU programs and assert on stdout and
  exit code.
- AddressSanitizer and UndefinedBehaviorSanitizer builds run the whole suite.
- A fuzz harness feeds random byte sequences to the verifier and random
  source text to the frontend, asserting no crash.

## 7. Bytecode format summary

SURU bytecode is a tight, typed stack bytecode. Instructions are one byte
opcodes with optional operands. The module header records version, entry point,
constant pool, and per-function metadata (frame size, max operand depth). The
verifier consumes the serialized form directly, so the format and the
verifier stay in lockstep.
