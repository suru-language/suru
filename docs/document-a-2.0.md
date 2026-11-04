# SURU Natural Syntax — Document A 2.0

**Status:** Approved language contract  
**Edition:** Natural Syntax 2.0  
**Date:** 2026-11-03, revised 2026-11-04  
**Bootstrap:** C17 legacy syntax remains the default during migration

This document defines the Natural Syntax Edition of SURU. Implementations must
follow this document; implementation convenience must not add alternate
spellings or silently assign meaning to unspecified constructs. The legacy
language described by `docs/spec.md` and `docs/grammar.md` remains supported
through the explicit `legacy` syntax mode.

The 2026-11-04 revision re-approves the revised design decisions — explicit
matching block terminators, `value`/`change`/`constant` with `set ... to` as
the only assignment statement, explicit public routine types,
`across`/`from`/`until`/`through` iteration, records with immutable-by-default
fields and statically checked contracts, exhaustive choice matching with
`Result`/`Option` compiler-known types, relative-then-`SURU_MOD_PATH` module
resolution, interpolation with `{{`/`}}` escaping, declared typed exceptions
with `attempt`/`handle`/`ensure`, UTF-8 lexical rules, `--syntax` CLI
selection, and the expanded lexer compatibility gate — as the language
contract for all subsequent documentation and implementation steps. The
compiler implements this approved specification; it does not invent the
language during implementation.

## 1. Source and lexical rules

Source is UTF-8 containing Unicode scalar values. Invalid UTF-8, U+0000, and
invalid Unicode escape sequences are errors. Identifiers are case-sensitive;
keywords are lowercase. An identifier starts with a Unicode XID_Start value and
continues with zero or more Unicode XID_Continue values. Names beginning with
`__` are reserved.

Whitespace includes spaces, tabs, carriage returns, and form feeds. `//`
comments extend to the line ending. `/* ... */` comments nest. A first-line
shebang beginning with `#!` is ignored. Comments and whitespace do not produce
tokens, except that a logical newline may terminate a statement as described
below.

The Natural Syntax keyword set is:

```text
across and as attempt bring case change choose constant contract each
end ensure exception expose false from give handle if in not through until
otherwise or record repeat routine set show supports true value when while
throws this none
```

`end` always takes a required block kind. The valid pairs are `end routine`,
`end record`, `end contract`, `end choice`, `end when`, `end each`,
`end repeat`, `end attempt`, `end choose`, and `end exception`.

### 1.1 Literals

```ebnf
integer       = decimal | hexadecimal | binary ;
decimal       = digit { digit | "_" } ;
hexadecimal   = ("0x" | "0X") hex-digit { hex-digit | "_" } ;
binary        = ("0b" | "0B") ("0" | "1") { ("0" | "1") | "_" } ;
float         = digit { digit | "_" } "." digit { digit | "_" }
                [ ("e" | "E") ["+" | "-"] digit { digit | "_" } ]
              | digit { digit | "_" } ("e" | "E") ["+" | "-"]
                digit { digit | "_" } ;
string        = '"' { string-char | escape | interpolation } '"' ;
interpolation = "{{" | "}}" | "{" expression "}" ;
escape        = "\\" ("n" | "t" | "r" | "0" | "\\" | '"' | "\'"
                | "u" hex-digit hex-digit hex-digit hex-digit) ;
```

`{{` and `}}` represent literal braces. An interpolated expression is parsed
using the normal expression grammar, evaluated once, and formatted from left
to right. Primitive values use the standard formatting contract. A custom
value must be explicitly converted before interpolation. A newline in a string
is invalid unless represented by an escape.

### 1.2 Logical lines

One logical statement normally occupies one physical line. A newline terminates
a statement unless it occurs inside balanced parentheses or brackets, or after
an explicit continuation marker `\\` followed by the newline. The continuation
marker is valid anywhere a statement may continue, including outside balanced
delimiters. Newlines inside braces belonging to a block are still logical
statement boundaries. Blank lines are ignored. A lexer must retain byte offset,
line, and Unicode scalar column for every token and diagnostic.

## 2. Formal grammar

The grammar uses EBNF: `?` is optional, `*` is zero or more, `+` is one or
more, and quoted text is a terminal. `NEWLINE` is the logical newline token.

### 2.1 Program and declarations

```ebnf
program          = { module-declaration | declaration | statement } ;
module-declaration = bring-declaration ;
declaration      = routine-declaration | record-declaration
                 | contract-declaration | choice-declaration
                 | constant-declaration ;

bring-declaration = "bring" string-literal ["as" identifier] terminator ;
visibility       = ["expose"] ;
constant-declaration = visibility "constant" identifier [":" type]
                       "=" expression terminator ;

routine-declaration = visibility "routine" identifier
                      "(" [parameter {"," parameter}] ")"
                      ["->" type] [throws-clause]
                      statement-block "end" "routine" terminator? ;
parameter        = identifier ":" type ;
throws-clause    = "throws" type-name {"," type-name} ;

record-declaration = visibility "record" identifier
                     ["supports" type-name {"," type-name}]
                     record-member* "end" "record" terminator? ;
record-member    = field-declaration | routine-declaration ;
field-declaration = ["change"] identifier ":" type terminator ;

contract-declaration = visibility "contract" identifier
                       contract-member* "end" "contract" terminator? ;
contract-member  = routine-signature ;
routine-signature = "routine" identifier
                    "(" [parameter {"," parameter}] ")"
                    ["->" type] [throws-clause] terminator ;

choice-declaration = visibility "choice" identifier
                     variant-declaration+
                     "end" "choice" terminator? ;
variant-declaration = identifier ["(" [parameter {"," parameter}] ")"]
                      terminator ;

terminator       = ";" | NEWLINE ;
type-name        = identifier ;
```

Public routine parameters, return types, record fields exposed through public
APIs, contract signatures, and choice payloads must have explicit types.

### 2.2 Statements

```ebnf
statement-block  = { statement } ;
statement        = binding-statement | assignment-statement
                 | give-statement | expression-statement
                 | when-statement | each-statement | repeat-statement
                 | choose-statement | attempt-statement
                 | show-statement | nested-routine-declaration
                 | terminator ;

binding-statement = value-binding | change-binding ;
value-binding    = "value" identifier [":" type] "=" expression terminator ;
change-binding   = "change" identifier [":" type] "=" expression terminator ;
assignment-statement = "set" assignment-target "to" expression terminator ;
assignment-target = identifier | postfix-expression ;
give-statement   = "give" [expression] terminator ;
expression-statement = expression terminator ;
show-statement   = "show" "(" expression ")" terminator ;

when-statement   = "when" expression statement-block
                   { "otherwise" "when" expression statement-block }
                   ["otherwise" statement-block]
                   "end" "when" terminator? ;

each-statement   = "each" identifier "across" expression statement-block
                 | "each" identifier "from" expression
                   ("until" | "through") expression statement-block
                 "end" "each" terminator? ;
repeat-statement = "repeat" "while" expression statement-block
                   "end" "repeat" terminator? ;

choose-statement = "choose" expression
                   case-clause+ "end" "choose" terminator? ;
case-clause      = "case" pattern statement-block ;
attempt-statement = "attempt" statement-block
                    handler-clause+
                    [ensure-clause]
                    "end" "attempt" terminator? ;
handler-clause   = "handle" identifier ":" type-name statement-block
                    "end" "exception" terminator? ;
ensure-clause    = "ensure" statement-block "end" "exception" terminator? ;

pattern          = identifier ["(" pattern-list ")"] | "_" ;
pattern-list     = pattern {"," pattern} ;
```

The `otherwise` clause belongs to the nearest unmatched `when`. An
`otherwise when` chain may contain any number of conditions and at most one
final `otherwise`. Every block terminator must match its opening construct.

### 2.3 Types and expressions

```ebnf
type             = primitive-type | array-type | function-type | type-name
                 | generic-type ;
primitive-type   = "Whole" | "Real" | "Bool" | "Text" | "Unit" ;
array-type       = "[" type "]" ;
function-type    = "routine" "(" [type {"," type}] ")" ["->" type] ;
generic-type     = type-name "<" type {"," type} ">" ;

expression       = assignment-expression ;
assignment-expression = conditional-expression ;
conditional-expression = logical-or ["?" expression ":" expression] ;
logical-or       = logical-and {"or" logical-and} ;
logical-and      = comparison {"and" comparison} ;
comparison       = bit-or {comparison-operator bit-or} ;
comparison-operator = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
bit-or           = bit-xor {"|" bit-xor} ;
bit-xor          = bit-and {"^" bit-and} ;
bit-and          = shift {"&" shift} ;
shift            = additive {("<<" | ">>") additive} ;
additive         = multiplicative {("+" | "-") multiplicative} ;
multiplicative   = unary {("*" | "/" | "%") unary} ;
unary            = ("-" | "not" | "~") unary | postfix-expression ;
postfix-expression = primary-expression
                     { call | index | field-access } ;
call             = "(" [expression {"," expression}] ")" ;
index            = "[" expression "]" ;
field-access     = "." identifier ;
primary-expression = literal | identifier | "this"
                   | "(" expression ")" | array-expression
                   | routine-expression ;
array-expression = "[" [expression {"," expression}] "]" ;
routine-expression = "routine" "(" [parameter {"," parameter}] ")"
                     ["->" type] statement-block "end" "routine" ;
literal          = integer | float | string | "true" | "false" | "none" ;
```

The precedence table, from lowest to highest, is:

| level | operators | associativity |
|---:|---|---|
| 1 | `? :` | right |
| 2 | `or` | left |
| 3 | `and` | left |
| 4 | `== != < <= > >=` | left |
| 5 | `\|` | left |
| 6 | `^` | left |
| 7 | `&` | left |
| 8 | `<< >>` | left |
| 9 | `+ -` | left |
| 10 | `* / %` | left |
| 11 | unary `- not ~` | right |
| 12 | calls, indexing, fields | left |

Natural Syntax assignment is a statement and is never an expression. `set`
requires `to`; `=` is not an assignment token in Natural Syntax.

## 3. Static semantics

`Whole` is signed 64-bit integer, `Real` is IEEE-754 binary64, `Bool` is a
boolean, `Text` is valid UTF-8 text, and `Unit` has one value. Arrays preserve
element order, and `each` iterates a list in element order. Iteration order is
deterministic; because arrays are fixed-length, a `set` of an element during
iteration is visible to subsequent iterations. String iteration yields Unicode
scalar values, not UTF-8 bytes, in code-point order.

`value` creates an immutable binding. `change` creates a mutable binding.
Fields are immutable unless declared with `change`. `constant` is module-scope
only and its initializer must be a compile-time expression. `set` may target
only a mutable binding, mutable field, or mutable indexed location. The right
side is type-checked against the target.

Public API types and all parameters are explicit. Local `value` and `change`
types may be inferred. A missing routine return type means `Unit`. A non-Unit
routine must give a value on every valid path. `give` without an expression is
valid only for Unit routines. Recursive routines must declare a return type.

Records contain fields and methods. `this` is the receiver. `supports` requires
the record to provide every compatible contract signature; composition is used
instead of implicit inheritance or multiple inheritance.

Choices are tagged variants. `choose` is a statement and must be exhaustive;
`case _` is the only wildcard. `Option<T>` and `Result<T,E>` are compiler-known
choices. Pattern matching is the only implicit unwrapping operation.

Modules resolve a relative `bring` path from the importing file first, then
through `SURU_MOD_PATH`. Missing modules, ambiguous identities, and cycles are
diagnostics. Declarations are private unless marked `expose`.

## 4. Effects and exceptions

Expected failures use `Result<T,E>`; optional values use `Option<T>`. Expected
errors should generally be modeled with `Result` rather than exceptions. A
routine that may propagate an exceptional failure lists its typed exceptions
after `throws`. Propagation of an undeclared exception is a compile-time error.
`handle name: Type` catches only compatible exceptions. `ensure` executes on
normal completion, return, and exception propagation. If cleanup raises while
another exception is active, the cleanup exception replaces the active
exception and the original is retained as a diagnostic cause. Nested `attempt`
blocks unwind innermost first.

## 5. Resource and diagnostic requirements

Implementations must reject source files larger than 64 MiB, nesting deeper
than 512 lexical or syntactic levels, more than 1,048,576 array elements, and
interpolation nesting deeper than 128 levels. These limits are implementation
limits of the Natural Syntax contract and must produce deterministic resource
diagnostics rather than process crashes.

Diagnostics must include a stable code, complete message, byte offset, line,
and Unicode scalar column. The conformance suite compares all of these fields.
Malformed UTF-8, invalid escapes, unterminated strings/comments, mismatched
block terminators, malformed interpolation, invalid assignment targets, and
non-exhaustive choices each require a diagnostic.

## 6. Migration and compatibility

The CLI accepts `--syntax=legacy|natural` for `check`, `run`, and `compile`.
Legacy remains the default. Natural Syntax must not change legacy token
meanings, bytecode semantics, or existing test outcomes. Natural Syntax may
become the default only after its lexer, parser, semantic, runtime, and module
conformance gates pass and a migration policy is published.

The C17 compiler remains the Stage-0 reference where semantics overlap. New
Natural Syntax behavior is checked against the conformance expectations in
`tests/natural/`; it is not represented as a legacy equivalent.
