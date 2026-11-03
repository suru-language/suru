# SURU Language Specification

Version 0.1.0 (legacy bootstrap reference)

SURU is an independent, statically typed, compiled general-purpose programming
language. It is implemented from scratch in ISO C17. This document is the
authoritative reference for the legacy bootstrap language: its syntax,
semantics, type system, memory model, and runtime behavior. The Natural Syntax
Edition is not yet specified in this repository; see
`docs/syntax-migration.md`. Builtin signatures and runtime behavior are listed
in `docs/builtins.md`.

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
- Shebang: a first line beginning with `#!` is ignored, so scripts can be made
  directly executable (e.g. `#!/usr/bin/env suru`).
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
and    break  continue  do  else  enum  false  for  func
if     in     let      mut  none  not   or     return  true
type   use    while    new  as
```

`new` and `as` are reserved for future use and currently have no syntax; using
them as identifiers is an error.

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
first-class values.

A lambda literal `fn(T1, T2) -> R { body }` creates a closure value of
function type. A closure may reference variables from its enclosing function
scopes; such variables are **captured by value** at the point the closure is
created (each captured variable is copied into the closure's environment).
The closure can then be stored, passed, and called anywhere, including after
the enclosing function has returned. Assigning to a captured variable updates
the closure's local copy only.

```
func make_adder(n: int) -> fn(int) -> int {
    return fn(x: int) -> int { return x + n; };   // captures n by value
}
func main() {
    let f = make_adder(10);
    println(to_str(f(5)));   // 15
}
```

Calls through function-typed values (variable, array element, record field, or
expression) are dispatched dynamically at runtime.

## 6.1 Generics

A function may declare type parameters and use them in parameter and return
types:

```
func id<T>(x: T) -> T { return x; }
func first<T>(a: [T]) -> T { return a[0]; }
```

A call to a generic function infers the type arguments from the argument
types and checks the call against the instantiated signature. The function
body is compiled once and is restricted to **type-independent operations**:
a type parameter may be passed around, stored in arrays/records, and returned,
but operations that require a concrete type (arithmetic, comparison, field
access, indexing into `T` itself) are rejected. Generic functions compose and
may return closures that capture values of a type parameter.

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

A function whose first parameter type is a record can be called in **method**
syntax: `obj.method(a, b)` desugars to `method(obj, a, b)`. The method name
must name such a function, and the named is evaluated once. If the name names a
record field instead, the field is called (so a field holding a closure may be
invoked with the same syntax):

```
func translate(p: Point, dx: int) -> Point { ... }
let q = p.translate(1);      // same as translate(p, 1)
```

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
  In a mixed `int`/`float` expression, the `int` operand is implicitly
  promoted to `float` (e.g. `1 + 2.5` is `3.5`).
- String concatenation: `+` on two `str` values concatenates them.
- String repetition: `str * n` (or `n * str`) repeats a string `n` times.
- Comparison: `== != < <= > >=` yield `bool`.
- Logical: `and or not` short-circuit.
- Conditional: `cond ? a : b` evaluates `a` if `cond` is true, else `b`;
  both branches must have the same type.
- Bitwise: `& | ^` and shifts `<< >>` on `int`; `~` is unary bitwise NOT.
  Shifts treat the operand as a 64-bit unsigned bit pattern (logical shifts,
  and a shift count of 64 or more yields 0). Precedence (loosest to tightest)
  follows C: `|`, `^`, `&`, `<<`/`>>`, then additive.
- Index: `a[i]` yields the element type, bounds-checked. A negative index
  counts from the end (`a[-1]` is the last element). Strings are indexable
  too: `s[i]` yields a one-character string.
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
- `do { ... } while (cond);` runs the body once, then repeats while `cond` is
  true.
- `for ch in str { ... }` iterates the characters (one-character strings) of a
  string.

## 6. Functions

```
func name(param-list?) -> return-type? block
param-list = param ("," param)*
param = identifier ":" type
block = "{" statement* "}"
```

A function body is a block. A value is produced only by an explicit `return`
statement. A `return` with no operand is legal only in a function whose return
type is `unit`. A function with a non-`unit` return type must have a `return`
on every control path; a body that can fall off the end is a compile-time
error. `main` is the program entry point and must return `unit` or `int` (an
`int` result becomes the process exit code).

Function declarations are hoisted within a module, so calls may appear before
their textual definition.

## 7. Program Structure

A program is a sequence of top-level items:

- functions (`func`)
- record types (`type`)
- constants (`const`)
- enumerations (`enum`)
- `use` includes (source-level module inclusion)
- a single `main` function (required)

A `use "path.suru"` directive parses another source file and merges its
top-level items into the current module. Paths are resolved (in order) as an
absolute path, relative to the containing file's directory, then against each
directory in the `SURU_MOD_PATH` environment variable (colon-separated).
Includes are idempotent, and recursive includes are bounded to prevent cycles.
This provides simple user-module composition.

`const` bindings are compile-time constants. Their initializers are constant
expressions: literals, references to previously declared constants, and
arithmetic/string/logical/bitwise operators over those. For example:

```
const BASE = 10;
const DOUBLE = BASE * 2;     // 20
const LABEL = "suru" + "-lang";
```

Forward references to not-yet-declared constants are an error.

An enumeration `enum Name { A, B, C };` declares its enumerators as integer
constants `0, 1, 2, ...`, usable anywhere a constant is:

```
enum Color { Red, Green, Blue };
let c = Green;      // 1
```

There is no global mutable state in the language; all state lives in
function-local bindings and heap objects.

The tracing garbage collector's initial trigger threshold is configurable via
the `SURU_GC_THRESHOLD` environment variable (bytes) for tuning and stress
testing.

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

- `print(v: any)` — write a value to stdout (int, float, str, bool, and
  recursively rendered arrays and records; cyclic structures are truncated).
- `println(v: any)` — same, with a trailing newline.
- `str_len(s: str) -> int` — length in bytes.
- `array_len(a: [T]) -> int` — element count (any element type).
- `to_str(v: any) -> str` — int/float/bool to string.
- `str_compare(a: str, b: str) -> int` — lexicographic (-1/0/1).
- `type_of(v: any) -> str` — runtime type tag: `int`, `float`, `str`,
  `array`, `record`, `closure` (bools are represented as `int` at runtime).
- `f64_to_str(f: float) -> str`.
- `read_line() -> str` — read a line from stdin ("" on EOF).
- `read_file(path: str) -> str` — read a whole file; returns `""` on error.
- `write_file(path: str, contents: str)` — write `contents` to a file
  (overwrites; fails silently on error).
- `file_exists(path: str) -> bool` — whether `path` exists.
- `int_to_str_base(i: int, base: int) -> str` — `i` in base `base`
  (2..36); negative numbers get a `-` prefix.
- `str_pad(s: str, width: int, fill: str) -> str` — right-pad `s` to at
  least `width` bytes using the first character of `fill`; unchanged if `s`
  is already `width` bytes or longer.
- `array_concat(a: [T], b: [T]) -> [T]` — a new array with `a` followed by
  `b` (any element type).
- `array_contains(a: [int], v: int) -> bool` — whether `v` appears in `a`.
- `array_index_of(a: [int], v: int) -> int` — the index of the first `v`
  in `a`, or `-1` if absent.
- `array_sort(a: [int]) -> [int]` — a new ascending-sorted copy of `a`.
- `make_array(n: int) -> [int]` — a zero-filled array of `n` elements.
- `range(lo: int, hi: int) -> [int]` — the integers `lo .. hi-1`.
- `range_step(lo: int, hi: int, step: int) -> [int]` — like `range`,
  with a (nonzero) step; a negative step counts down.
- `substr(s: str, start: int, len: int) -> str` — substring, bounds-clamped.
- `str_contains(haystack: str, needle: str) -> bool`.
- `abs(i: int) -> int`, `min(a: int, b: int) -> int`,
  `max(a: int, b: int) -> int`.
- `str_to_f64(s: str) -> float` — parse a float from a string.
- `int_from_str(s: str) -> int` — parse a signed decimal integer (optional
  `+`/`-` sign); returns `0` if the string does not parse as an integer,
  traps on overflow.
- `assert(cond: bool, msg: str)` — trap with `msg` if `cond` is false.
- `exit(code: int)` — terminate the program immediately with `code`.
- `to_hex(i: int) -> str` — lowercase hexadecimal (64-bit two's complement).
- `to_bin(i: int) -> str` — binary.
- `f64_to_str_prec(f: float, prec: int) -> str` — fixed-point formatting with
  `prec` digits after the decimal point (clamped to 0..20).
- `str_repeat(s: str, n: int) -> str` — `s` repeated `n` times.
- `str_trim(s: str) -> str` — `s` with leading/trailing ASCII whitespace removed.
- `str_split(s: str, sep: str) -> [str]` — split `s` on `sep` (non-empty).
- `str_join(arr: [str], sep: str) -> str` — join `arr` with `sep`.
- `str_reverse(s: str) -> str`, `str_upper(s: str) -> str`,
  `str_lower(s: str) -> str` — ASCII transforms.
- `slice(a: [T], start: int, len: int) -> [T]` — bounds-clamped copy of a
  subarray.
- `len(x)` — element count of an array or byte length of a string.
- `array_reverse(a: [T]) -> [T]` — a new array with elements reversed.
- `array_sum(a: [int]) -> int`, `array_min(a: [int]) -> int`,
  `array_max(a: [int]) -> int` — reductions (traps on an empty array).
- `args_len() -> int` — number of command-line arguments passed to the program.
- `args(i: int) -> str` — the `i`-th command-line argument (0-based; traps if
  out of range). Arguments are passed after the source file on the CLI, e.g.
  `suru run prog.suru a b`.
- `f64_to_i64`, `i64_to_f64` conversions.

The set is intentionally minimal; it will grow through the release roadmap.

## 10. Errors and Diagnostics

Compile-time errors are reported with source location (file, line, column)
and a code. The compiler never crashes on malformed input; it collects
diagnostics and exits nonzero. Diagnostics render the offending source line
along with a caret at the error column; a preceding line is included when the
error falls on a later line. A diagnostic may carry one or more `note:` lines
(secondary diagnostics with their own caret), e.g. to show the type of an
offending operand. Notes never count as errors. When a name (variable,
function, type, or constant) is misspelled, a `note:` suggests the closest
matching declared name. Runtime traps (bounds, division by zero,
overflow) abort execution with a message and a nonzero exit code.

## 11. Future Work

See `docs/roadmap.md`.
