# Natural Syntax Migration

## Reconciliation status

The legacy language accepted by the C17 bootstrap remains specified by
`docs/spec.md` and `docs/grammar.md`. The approved Natural Syntax Edition is
defined by `docs/document-a-2.0.md`.

Document A 2.0 records the approved decisions for matching block terminators,
`value`/`change`/`set`, explicit routine types, `across`/`from`/`until`/
`through` iteration, records and contracts, choices, modules, interpolation,
typed exceptions, and UTF-8/newline behavior. Natural syntax is now specified;
its lexer gate passes, while parser and semantic implementation remain future
gates. Legacy remains the compatibility baseline.

## Phase 1 status

**Complete:** Document A 2.0 is approved, the formal EBNF and precedence table
are published, lexical/type/effect rules and resource limits are documented,
positive and negative conformance fixtures exist, and the Natural Syntax lexer
passes its expanded gate. Parser, semantic, and CLI integration are the next
phase and are not represented as complete here.

## Decisions recorded in Document A 2.0

| natural form | decision |
|---|---|
| `value` / `change` / `constant` / `set ... to` | Immutable/mutable bindings, module-scope compile-time constants, and statement-only assignment. |
| `routine` / `give` | Explicit public API types, Unit default, path-complete non-Unit returns, and typed `throws`. |
| `when` / `otherwise` | Explicit `end when`; nearest unmatched owner; one final `otherwise`. |
| `each` / `repeat` | Ordered `across`, exclusive `until`, inclusive `through`, and `repeat while`. |
| `record` / `contract` | Record-owned methods, `this`, explicit mutable fields, and static `supports`. |
| `choice` / `choose` | Typed payload variants, exhaustive statement matching, and explicit `_` wildcard. |
| `bring` / `expose` | Relative-then-`SURU_MOD_PATH` resolution, private-by-default visibility, and cycle diagnostics. |
| interpolation | Normal expressions in braces, escaped braces, left-to-right exactly-once evaluation. |
| typed exceptions | Declared propagation, typed handlers, and `ensure` on return/normal/exception paths. |

The complete lexical grammar, EBNF, precedence table, static type rules, effect
rules, resource limits, diagnostics, and migration policy are in
`docs/document-a-2.0.md`. They are intentionally not added to the legacy
grammar.

## Migration strategy

1. Freeze and publish the Natural Syntax EBNF and precedence table. **Done in
   Document A 2.0.**
2. Add natural lexer token kinds and source-location tests without changing
   legacy token meanings.
3. Add `--syntax=legacy|natural` to `check`, `run`, and `compile`; legacy stays
   the default.
4. Port parser productions in small feature groups with positive and negative
   tests and reference-output comparisons.
5. Port semantic and bytecode behavior only after each production is specified.
6. Keep legacy tests until the deprecation/removal policy is approved.

The CLI spelling is approved by Document A but is not implemented yet.

## Natural Syntax conformance corpus

`tests/natural/` contains approved positive and negative fixtures for lexical,
statement, record/choice, module, interpolation, UTF-8, terminator,
exhaustiveness, and mutability behavior. New syntax expectations are
conformance expectations rather than legacy differential cases. The corpus must
be extended whenever a new production or diagnostic is implemented.

## Lexer compatibility gate

The transitional source manager, diagnostics, lexer, and structured legacy
token stream in `selfhost/compiler/` continue to be tested against the C lexer
by `make selfhost-test`. That comparison covers token kind and line/column
location, shebangs, nested comments, keywords, literals, operators, source
boundaries, decoded literal payloads, and classified legacy diagnostics.

The Natural Syntax gate must additionally compare token kind, token text,
decoded literal payload, start/end offsets, line/column, diagnostic code,
complete message, and diagnostic location. It covers all natural keywords,
identifiers, numeric and interpolated strings, Unicode and malformed UTF-8,
escapes, nested comments, shebangs, operators, statement boundaries, resource
limits, and invalid input. The C17 compiler remains the reference where
semantics overlap; new syntax uses the approved expectations in
`tests/natural/`.

The Natural Syntax lexer is implemented in
`selfhost/compiler/natural_lexer.suru` and its approved lexical corpus passes
through `make selfhost-test`, including Unicode scalar locations, malformed
UTF-8, interpolation, nested comments, keyword coverage, resource limits, and
stable lexical diagnostic messages. The parser port is the next phase;
the initial structural parser gate is now in
`selfhost/compiler/natural_parser.suru`. `--syntax=natural` is not enabled
until expression/type parsing and semantic gates exist. The structural gate
currently checks declarations, binding initializers, public signatures,
constant scope, mutable assignment targets, and routine return context. The
token-based expression parser and core statement AST bridge now cover
precedence expressions, bindings, assignment, `show`, and `give`; type and
semantic gates remain open. Control-flow AST nodes and nested block ownership
are covered by the Natural Syntax parser gate. The initial semantic gate now
covers local symbol/type inference, assignment mutability, expression operand
types, and Boolean conditions. Full records, choices, modules, effects, and
runtime integration remain open. No legacy default behavior may change during
the port.
