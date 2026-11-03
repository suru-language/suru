# Natural Syntax Migration

## Reconciliation status

The repository contains `docs/spec.md` and `docs/grammar.md` for the legacy
syntax accepted by the C17 bootstrap. It does not contain a complete approved
Document A 2.0 defining the Natural Syntax Edition's grammar, lexical rules,
type/effect rules, or block-termination rules. The feature-name list in the
self-hosting request is not sufficient to derive those rules without inventing
language behavior.

Consequently, natural syntax is a design gate, not an implemented feature at
the audited revision. The legacy compiler and tests remain the compatibility
baseline.

## Questions requiring Document A decisions

| requested form | legacy analogue | decision required before implementation |
|---|---|---|
| `value` | `let` | binding grammar, type ascription, and initializer terminator |
| `change` | `mut` | reassignment and capture semantics |
| `constant` | `const` | module-only or local scope and constant-expression rules |
| `set` | assignment | whether it is a statement keyword or expression operator |
| `routine` / `give` | `func` / `return` | parameter, return, and implicit-unit rules |
| `when` / `otherwise` | `if` / `else` | nesting and block terminators |
| `each` / `repeat` | `for` / `while` | iteration protocol and loop result semantics |
| `record` | `type` | fields, methods, mutability, and construction syntax |
| `contract` / `supports` | no exact analogue | declaration, generic constraints, and dispatch semantics |
| `choice` / `choose` | `enum` / no exact analogue | patterns, exhaustiveness, and payload layout |
| `bring` / `expose` | `use` / no export system | module paths, visibility, and cycles |
| interpolation | ordinary strings | escape/interpolation boundaries and evaluation order |
| typed exceptions | runtime traps | effect typing, propagation, and bytecode representation |

The coordination rule is strict: these choices must be settled in Document A,
then reflected together in `docs/grammar.md`, the lexer/parser, and positive
and negative conformance tests.

## Proposed decisions for Document A review

The following is a design proposal, not implemented behavior:

- Natural syntax uses one statement per physical line; multiline expressions
  continue inside balanced delimiters or after an explicit continuation marker.
- `routine`, `record`, `contract`, `choice`, `when`, `each`, and `repeat` use
  `end` as their explicit terminator. `otherwise` belongs to the nearest open
  `when`; an `otherwise when` chain is permitted.
- `value` declares an immutable binding, `change` declares a mutable binding,
  `constant` declares a compile-time binding, and `set target = expression`
  is the only assignment statement.
- `routine name(parameters) -> result` is the declaration form and `give`
  supplies its result. A routine with no result type has unit result and may
  use `give` without an expression.
- `each item in iterable` is the array/string iteration form. `repeat
  condition` is a pre-test loop. Neither form has an implicit value.
- `record` replaces `type` for user records; methods are declarations whose
  first parameter is the receiver. `supports` is a declared contract
  constraint checked during semantic analysis, not dynamic inheritance.
- `choice` cases are named variants with optional payloads; `choose` must be
  exhaustive unless it contains an explicit wildcard case.
- `bring` imports a module and `expose` marks a public declaration. Imports
  are resolved relative to the importing file, then through `SURU_MOD_PATH`,
  and cycles are diagnosed.
- `Result<T, E>` and `Option<T>` are ordinary compiler-known choice types;
  pattern matching is the only implicit unwrapping operation.
- Exceptions are typed effects on routines. A routine either handles an
  effect locally or declares it in its signature; an undeclared propagation
  is a compile-time error.

Document A must accept, revise, or reject these proposals before the natural
lexer is implemented. They are intentionally not added to the legacy grammar.

## Migration strategy

1. Freeze and publish the Natural Syntax EBNF and precedence table.
2. Add lexer token kinds and source-location tests without changing legacy
   token meanings.
3. Add an explicit syntax selection option to `check`, `run`, and `compile`.
   Legacy remains the default during migration.
4. Port parser productions in small feature groups, with both positive and
   negative tests and reference-output comparisons.
5. Port semantic and bytecode behavior only after each production is specified.
6. Keep the old syntax tests until the deprecation/removal policy is approved.

The proposed CLI spelling is `--syntax=legacy|natural`; it is not implemented
until the existing command-line conventions and Document A approve it.

## Lexer compatibility gate

The transitional source manager and lexer in `selfhost/compiler/` are tested on
the same valid corpus as the C lexer by `make selfhost-test`. The current comparison
covers token kind and line/column location, including shebangs, nested block
comments, keywords, literals, operators, source boundaries, and decoded
literal payloads. The negative corpus currently checks a reserved `__`
identifier, missing hexadecimal/binary digits, invalid escapes, an
unterminated string, and unexpected punctuation, including diagnostic codes
and locations. Before this gate can be called complete, compare full
diagnostic messages on a broader corpus. Document A must additionally define
malformed UTF-8, string interpolation, and natural-syntax resource limit
behavior. Only after that expanded gate passes should a large parser
replacement begin.
