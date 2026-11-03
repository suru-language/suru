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

The first self-hosted lexer must be tested on the same corpus as the C lexer.
For every input, compare token kind, decoded literal bytes/value, byte offset,
line/column, and diagnostics. Include malformed UTF-8/escapes as required by
Document A, nested block comments, both comment styles, numeric boundaries,
interpolation, and resource limits. Only after this gate passes should a large
parser replacement begin.
