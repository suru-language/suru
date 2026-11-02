# SURU Formal Grammar

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
```

## 2. Program structure

```
program       = { item }
item          = func_decl | type_decl | const_decl | use_decl
func_decl     = "func" identifier [ param_list ] [ "->" type ] block
use_decl      = "use" string_literal ";"
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
              | while_stmt | for_stmt | break_stmt | continue_stmt | block
let_stmt      = ("let" | "mut") identifier [ ":" type ] "=" expression ";"
expr_stmt     = expression ";"
return_stmt   = "return" [ expression ] ";"
if_stmt       = "if" expression block [ "else" (if_stmt | block) ]
while_stmt    = "while" expression block
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
assignment = logical_or [ assign_op logical_or ]
assign_op  = "=" | "+=" | "-=" | "*=" | "/="
logical_or = logical_and { "or" logical_and }
logical_and = comparison { "and" comparison }
comparison = additive [ ("==" | "!=" | "<" | "<=" | ">" | ">=") additive ]
additive   = multiplicative { ("+" | "-") multiplicative }
multiplicative = unary { ("*" | "/" | "%") unary }
unary      = ("-" | "not") unary | postfix
postfix    = primary { ("(" arg_list ")" | "[" expression "]" | "." identifier) }
primary     = int_literal | float_literal | string_literal | bool_literal
            | "none" | identifier | "(" expression ")"
            | array_literal
array_literal = "[" [ expression { "," expression } ] "]"
bool_literal = "true" | "false"
arg_list    = [ expression { "," expression } ]
```

## 6. Ambiguity resolution

- `if`/`while`/`for` statements greedily take a single expression as their
  condition. The `{` that begins the body separates the condition from the
  following statements.
- A `{` that appears where an expression is expected introduces a block
  expression; blocks are allowed only in statement position in the current
  grammar.
- `else` binds to the nearest preceding unmatched `if`.
