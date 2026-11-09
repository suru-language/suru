#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_ast_driver.suru" \
    "$ROOT/tests/natural/ast_statements_valid.suru" >"$tmp/valid.out"
grep -q '^ast|binding|1|value|answer|:Whole|(1 \+ (2 \* 3))$' "$tmp/valid.out"
grep -q '^ast|binding|2|change|score|:Whole|100$' "$tmp/valid.out"
grep -q '^ast|assignment|3|score|(score \+ 50)$' "$tmp/valid.out"
grep -q '^ast|show|4|"ready"$' "$tmp/valid.out"
grep -q '^ast|give|5|answer$' "$tmp/valid.out"

"$SURU" run "$DIR/natural_ast_driver.suru" \
    "$ROOT/tests/natural/ast_routine_expression_valid.suru" >"$tmp/routine.out"
grep -q '^ast|binding|1|value|f|=|routine(x: Whole) -> Whole$' "$tmp/routine.out"
grep -q '^ast|binding|4|value|g|=|routine() -> Unit$' "$tmp/routine.out"

if "$SURU" run "$DIR/natural_ast_driver.suru" \
       "$ROOT/tests/natural/ast_statement_invalid.suru" >"$tmp/invalid.out"; then
    echo "natural AST parser: invalid binding was accepted" >&2
    exit 1
fi
grep -q '^ast-error|1|binding requires an initializer$' "$tmp/invalid.out"

echo "natural AST parser: statement node corpus passed"
