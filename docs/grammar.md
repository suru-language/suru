# SURU Formal Grammar — Legacy Edition

> **Status:** This file describes the implemented legacy grammar (the grammar
> accepted by the C17 bootstrap at commit `b467901`). The Natural Syntax Edition
> named by the self-hosting plan is defined separately by the approved
> `docs/document-a-2.0.md`. Natural keywords are not silently added to this
> legacy grammar.

This is the normative grammar for SURU, written in EBNF. In the productions
below, terminal symbols are quoted or shown in `monospace`; nonterminals are
in italics. The constructs `?`, `*`, and `+` denote optional, zero-or-more,
and one-or-more repetitions respectively, and parentheses group alternatives.

## 1. Lexical grammar

```
source_char  = ? any Unicode scalar value except U+0000 ?
line_comment = "//" ?characters up to end of line?
block_comment = "/*" { block_comment | ?any char except "*/"? } "*/"
whitespace   = ? Unicode white space characters ?

identifier    = letter { letter | digit | "_" }
letter        = "a".."z" | "A".."Z" | "_"
digit         = "0".."9"
hex_digit     = digit | "a".."f" | "A".."F"

int_literal   = ("0x" hex_digit+ | "0b" ("0"|"1")+ | digit { (digit | "_") })
              | "_"* digit { (digit | "_") }
float_literal = digit { digit | "_" } "." digit { digit | "_" }
              [ ("e"|"E") ["+"|"-"] digit+ ]
              | digit { digit | "_" } ("e"|"E") ["+"|"-"] digit+
string_literal = '"' { ?any char except '"' or newline or unescaped backslash?
                      | escape } '"'
escape        = "\\" ("n" | "t" | "r" | "0" | "\\" | '"' | "'" | "u" hex_digit{4})

keyword       = "break" | "continue" | "const" | "else" | "false" | "for"
              | "func" | "if" | "in" | "let" | "mut" | "none" | "return"
              | "true" | "type" | "use" | "while" | "new" | "as"
              | "enum" | "do" | "and" | "or" | "not"
```

## 2. Program structure

```
program       = { item }
item          = func_decl | type_decl | const_decl | use_decl | enum_decl
func_decl     = "func" identifier [ "<" identifier { "," identifier } ">" ]
              [ param_list ] [ "->" type ] block
use_decl      = "use" string_literal ";"
enum_decl     = "enum" identifier "{" identifier { "," identifier } "}" ";"
type_decl     = "type" identifier "{" { field_decl } "}"
field_decl    = "let" identifier ":" type ";"
const_decl    = "const" identifier [ ":" type ] "=" literal ";"
literal       = [ "-" ] ( int_literal | float_literal )
              | string_literal | bool_literal
param_list     = "(" [ param { "," param } ] ")"
param         = identifier ":" type
block         = "{" { statement } "}"
```

## 3. Statements

```
statement     = let_stmt | expr_stmt | return_stmt | if_stmt
              | while_stmt | do_stmt | for_stmt | break_stmt | continue_stmt
              | block
let_stmt      = ("let" | "mut") identifier [ ":" type ] "=" expression ";"
expr_stmt     = expression ";"
return_stmt   = "return" [ expression ] ";"
if_stmt       = "if" expression block [ "else" (if_stmt | block) ]
while_stmt    = "while" expression block
do_stmt       = "do" block "while" "(" expression ")" ";"
for_stmt      = "for" identifier "in" expression block
break_stmt    = "break" ";"
continue_stmt = "continue" ";"
```

## 4. Types

```
type          = primitive | array_type | fn_type | record_type
primitive     = "int" | "float" | "bool" | "str" | "unit"
array_type    = "[" type "]"
fn_type       = "fn" "(" [ type { "," type } ] ")" [ "->" type ]
record_type   = identifier
```

## 5. Expressions

Precedence increases from lowest to highest.

```
expression = assignment
assignment = conditional [ assign_op conditional ]
assign_op  = "=" | "+=" | "-=" | "*=" | "/="
conditional = logical_or [ "?" assignment ":" conditional ]
logical_or = logical_and { "or" logical_and }
logical_and = comparison { "and" comparison }
comparison = bitor [ ("==" | "!=" | "<" | "<=" | ">" | ">=") bitor ]
bitor      = bitxor { "|" bitxor }
bitxor     = bitand { "^" bitand }
bitand     = shift { "&" shift }
shift      = additive { ("<<" | ">>") additive }
additive   = multiplicative { ("+" | "-") multiplicative }
multiplicative = unary { ("*" | "/" | "%") unary }
unary      = ("-" | "not" | "~") unary | postfix
postfix    = primary { ("(" arg_list ")" | "[" expression "]" | "." identifier
            | record_literal) }
primary     = int_literal | float_literal | string_literal | bool_literal
            | "none" | identifier | "(" expression ")"
            | array_literal | lambda_literal
lambda_literal = "fn" param_list [ "->" type ] block
record_literal = identifier "{" [ record_field { "," record_field } ] "}"
record_field = identifier "=" expression
array_literal = "[" [ expression { "," expression } ] "]"
bool_literal = "true" | "false"
arg_list    = [ expression { "," expression } ]
```

## 6. Operator precedence and associativity

Precedence increases from lowest to highest. Assignment and conditional
expressions are right-associative; all other binary operators are
left-associative. Postfix operators associate left-to-right.

| precedence | operators | associativity |
|---:|---|---|
| 1 | `=` `+=` `-=` `*=` `/=` | right |
| 2 | `? :` | right |
| 3 | `or` | left |
| 4 | `and` | left |
| 5 | `==` `!=` `<` `<=` `>` `>=` | left |
| 6 | `|` | left |
| 7 | `^` | left |
| 8 | `&` | left |
| 9 | `<<` `>>` | left |
| 10 | `+` `-` | left |
| 11 | `*` `/` `%` | left |
| 12 | unary `-` `not` `~` | right |
| 13 | calls, indexing, field access | left |

The grammar above is the authoritative parser shape; this table makes the
precedence implicit in the productions explicit for compiler ports and test
generators.

## 7. Ambiguity resolution

- `if`/`while`/`for` statements greedily take a single expression as their
  condition. The `{` that begins the body separates the condition from the
  following statements.
- A `{` that appears where an expression is expected introduces a block
  expression; blocks are allowed only in statement position in the current
  grammar.
- `else` binds to the nearest preceding unmatched `if`.

- A semicolon is required after every declaration or statement that is
  terminated by `;`; newlines are whitespace in the legacy grammar.
- `func` and `fn` are accepted as the function keyword by the lexer. The AST
  and diagnostics use `func`.
- `use` includes are resolved relative to the containing file first, then by
  `SURU_MOD_PATH`; an already included path is ignored.
- A record literal is recognized after an identifier only when the following
  token is `{` in expression position. A block is only valid in statement
  position.
