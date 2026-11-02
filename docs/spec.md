# SURU Language Specification

Version 0.1.0 (Draft)

SURU is an independent, statically typed, compiled general-purpose programming
language. It is implemented from scratch in ISO C17. This document is the
authoritative reference for the SURU language: its syntax, semantics, type
system, memory model, and runtime behavior.

## 1. Overview

SURU is designed around the following principles:

- **Independence.** SURU is not a fork, rename, or wrapper of any other
  language. Its syntax, bytecode, virtual machine, and runtime are all
  original implementations.
- **Memory safety.** The language exposes no raw pointers. All heap data is
  managed by a custom tracing garbage collector. Arrays are bounds-checked.
  Arithmetic is overflow-checked in debug builds and saturating/checked where
  required.
- **Simplicity.** A small, orthogonal core: functions, records, arrays,
  control flow, and a small standard library.
- **Compilability.** Programs compile to a compact, architecture-independent
  bytecode that is verified before execution by a dedicated verifier.

### 1.1 Notation

Grammar productions use EBNF (see `docs/grammar.md`). Keywords are shown in
`monospace`. The meta-notation `?` means optional, `*` means zero or more, and
`+` means one or more.

## 2. Lexical Structure

SURU source is a sequence of Unicode code points, though only ASCII is
required in the reference implementation for identifiers, operators, and
punctuation. Source files are UTF-8.

### 2.1 Comments

- Line comment: `//` to end of line.
- Block comment: `/* ... */`, may nest.

### 2.2 Identifiers

An identifier is a Unicode XID-start character followed by zero or more
XID-continue characters. The reference implementation accepts ASCII
letters, digits, and underscore, with the constraint that an identifier may
not begin with a digit or begin with two underscores (reserved).

```
identifier = letter (letter | digit | "_")*
letter     = "a".."z" | "A".."Z" | "_"
digit      = "0".."9"
```

### 2.3 Keywords

The following words are reserved and may not be used as identifiers:

```
and    break  continue  else  false  for  func
if     in     let      mut   none   not  or
return true   type     use   while  new  as
```

### 2.4 Literals

| Kind | Example | Type |
|------|---------|------|
| Integer | `42`, `0`, `-7`, `1_000` | `int` |
| Float | `3.14`, `-0.5`, `1e3`, `1.5e-2` | `float` |
| Boolean | `true`, `false` | `bool` |
| String | `"hi"`, `"a\nb"` | `str` |
| Unit | `none` | `unit` |

Integer literals are decimal by default; `0x` prefix means hexadecimal and
`0b` means binary. Underscores may separate digit groups. Integer literals
outside the range of a signed 64-bit value are an error.

String literals support these escapes: `\n`, `\t`, `\r`, `\\`, `\"`,
`\'`, `\0`, and `\uXXXX` for a Unicode code point. A string may not contain
an unescaped newline.

## 3. Types

SURU is statically typed. The type system is:

```
type = primitive | array | fn | record
primitive = "int" | "float" | "bool" | "str" | "unit"
array = "[" type "]"
fn = "fn" "(" param-list? ")" ("->" type)?
record = identifier            ; named record type
```

### 3.1 Primitive types

| Type | Description |
|------|-------------|
| `int` | Signed 64-bit integer. |
| `float` | IEEE-754 binary64 (double). |
| `bool` | `true` or `false`. |
| `str` | Immutable UTF-8 string. |
| `unit` | The type of expressions with no value. |

### 3.2 Arrays

`[T]` is a mutable, resizable array of `T`. The length is a runtime property,
not part of the static type. Array indexing is zero-based and bounds-checked
at runtime; an out-of-range access traps.

An array literal `[...]` builds an array from its elements, whose types must
all match; the element type is inferred from the first element. An empty
array literal requires a type ascription to fix the element type:

```
let primes = [2, 3, 5, 7];      // [int]
let floats = [1.5, 2.5];        // [float]
let empty: [int] = [];          // empty [int] array
println(to_str(primes[0]));      // 2
```

Array element types are unrestricted, so `[str]`, `[float]`, and nested
arrays like `[[int]]` are all valid. Array builtins such as `array_len`
accept any array, regardless of element type.

### 3.3 Function types

`fn(T1, T2) -> R` is the type of a function taking `T1, T2` and returning
`R`. A function type with no arrow returns `unit`. Function types are
first-class values; closures capture their environment by reference.

### 3.4 Records

A record is a named, heterogeneous aggregate declared with `type`:

```
type Pair {
    let left: int;
    let right: int;
}
```

Record fields are accessed with `.`. Records are heap-allocated GC objects.

A record value is created with a positional constructor call (`Pair(3, 4)`)
or with a named-field literal, which allows fields in any order and is
checked for missing/unknown/duplicate fields:

```
let p = Pair { right = 4, left = 3 };   // same as Pair(3, 4)
```

All fields must be present exactly once.

## 4. Type Ascription

The binding operator `:` ascribes a type:

```
let x: int = 42;
```

If a type ascription is absent, the type is inferred from the initializer.
`let` binds an immutable value; `mut` binds a mutable one.

## 5. Expressions

Precedence, highest to lowest:

1. Primary: literals, `(` expr `)`, identifiers, calls, field access, index
2. Unary: `-`, `not`
3. Multiplicative: `*`, `/`, `%`
4. Additive: `+`, `-`
5. Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
6. Logical AND: `and`
7. Logical OR: `or`
8. Assignment: `=`, `+=`, `-=`, `*=`, `/=`

### 5.1 Operators

- Arithmetic: `+ - * / %` on `int` and `float` (no `%` on `float`).
- String concatenation: `+` on two `str` values concatenates them.
- Comparison: `== != < <= > >=` yield `bool`.
- Logical: `and or not` short-circuit.
- Index: `a[i]` yields the element type, bounds-checked.
- Field: `r.f`.
- Assignment: `=` stores into a mutable binding, array element, or record
  field. Compound assignments (`+=` etc.) desugar to `lhs = lhs op rhs`.

Division by zero traps at runtime. In debug builds, signed overflow traps; in
release builds, signed arithmetic is defined as two's-complement wrapping.

### 5.2 Calls

A call `f(a, b)` evaluates the callee and arguments left to right. Calling a
value whose type is not a function type is a static error, and the verifier
re-checks the call site's operand types.

### 5.3 Control expressions

- `if cond { ... } else { ... }` is an expression; both branches must agree on
  a type (which may be `unit`).
- `while cond { ... }` loops. `break` exits the innermost loop; `continue`
  jumps to the loop condition.
- `for x in arr { ... }` iterates array elements by reference.

## 6. Functions

```
func name(param-list?) -> return-type? block
param-list = param ("," param)*
param = identifier ":" type
block = "{" statement* "}"
```

A function body is a block. The final expression or an explicit `return`
provides the result. A `return` with no operand is legal only in a function
whose return type is `unit`. `main` is the program entry point and must return
`unit` or `int` (an `int` result becomes the process exit code).

Function declarations are hoisted within a module, so calls may appear before
their textual definition.

## 7. Program Structure

A program is a sequence of top-level items:

- functions (`func`)
- record types (`type`)
- constants (`const`)
- `use` includes (source-level module inclusion)
- a single `main` function (required)

A `use "path.suru"` directive parses another source file and merges its
top-level items into the current module. Paths are resolved relative to the
containing file's directory, includes are idempotent, and recursive includes
are bounded to prevent cycles. This provides simple user-module composition
until the full package system ships.

There is no global mutable state in the language; all state lives in
function-local bindings and heap objects.

## 8. Memory Model

See `docs/memory.md` for the full model. Summary:

- Immediate values (`int`, `float`, `bool`) live on the VM operand stack or in
  stack frames.
- Heap objects (`str`, arrays, records, closures) are allocated on a custom
  managed heap and are reachable only through GC-managed roots.
- The collector is a tracing, stop-the-world mark-sweep with a nursery.
- The language has no `free`; object lifetime is automatic.

### 8.1 Object graph

The heap tracks object pointers. Because string data, array element buffers,
and record fields all contain either immediates or object pointers, the
collector can traverse and mark every live object. Bounds are maintained per
allocation so the collector never reads past an object.

## 9. Standard Library

The standard library provides a small set of builtin functions (see
`src/stdlib.c` and `docs/architecture.md`):

- `print(v: any)` — write a scalar value to stdout (int, float, str).
- `println(v: any)` — same, with a trailing newline.
- `str_len(s: str) -> int` — length in bytes.
- `array_len(a: [T]) -> int` — element count (any element type).
- `to_str(i: int) -> str`.
- `f64_to_str(f: float) -> str`.
- `read_line() -> str` — read a line from stdin ("" on EOF).
- `make_array(n: int) -> [int]` — a zero-filled array of `n` elements.
- `range(lo: int, hi: int) -> [int]` — the integers `lo .. hi-1`.
- `range_step(lo: int, hi: int, step: int) -> [int]` — like `range`,
  with a (nonzero) step; a negative step counts down.
- `substr(s: str, start: int, len: int) -> str` — substring, bounds-clamped.
- `str_contains(haystack: str, needle: str) -> bool`.
- `abs(i: int) -> int`, `min(a: int, b: int) -> int`,
  `max(a: int, b: int) -> int`.
- `str_to_f64(s: str) -> float` — parse a float from a string.
- `assert(cond: bool, msg: str)` — trap with `msg` if `cond` is false.
- `exit(code: int)` — terminate the program immediately with `code`.
- `to_hex(i: int) -> str` — lowercase hexadecimal (64-bit two's complement).
- `to_bin(i: int) -> str` — binary.
- `f64_to_str_prec(f: float, prec: int) -> str` — fixed-point formatting with
  `prec` digits after the decimal point (clamped to 0..20).
- `str_repeat(s: str, n: int) -> str` — `s` repeated `n` times.
- `str_trim(s: str) -> str` — `s` with leading/trailing ASCII whitespace removed.
- `f64_to_i64`, `i64_to_f64` conversions.

The set is intentionally minimal; it will grow through the release roadmap.

## 10. Errors and Diagnostics

Compile-time errors are reported with source location (file, line, column)
and a code. The compiler never crashes on malformed input; it collects
diagnostics and exits nonzero. Runtime traps (bounds, division by zero,
overflow) abort execution with a message and a nonzero exit code.

## 11. Future Work

See `docs/roadmap.md`.
