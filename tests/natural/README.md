# Natural Syntax conformance corpus

These files are normative Natural Syntax 2.0 examples. They are intentionally
not passed to the legacy C17 parser: legacy syntax remains the default and the
Natural Syntax parser is introduced only behind `--syntax=natural`.

Each positive source has a `.out` file containing the required observable
output. Each negative source has a `.diag` file containing the required stable
diagnostic class and location. The lexer gate must compare token kind, text,
decoded value, byte start/end offsets, line/scalar-column, diagnostic code, complete
message, and diagnostic location.

The corpus is divided into lexical, statement, type/effect, and module cases so
parser implementation can proceed in small groups after the Natural Syntax
lexer gate passes.
