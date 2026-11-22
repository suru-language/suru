#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for input in expression_valid.suru expression_postfix_valid.suru \
             expression_ternary_valid.suru; do
    "$SURU" run "$DIR/natural_expression_driver.suru" \
        "$ROOT/tests/natural/$input" >"$tmp/$input.out"
    grep -q '^expr|' "$tmp/$input.out"
done

"$SURU" run "$DIR/natural_token_driver.suru" \
    "$ROOT/tests/natural/expression_valid.suru" >"$tmp/tokens.out"
grep -q '^3|1|1|0|1|1$' "$tmp/tokens.out"
grep -q '^6|1|16|15|18|and$' "$tmp/tokens.out"
grep -q '^1|1|24|23|24|' "$tmp/tokens.out"
grep -q '^0|2|1|24|24|' "$tmp/tokens.out"

grep -q '^expr|(((1 + (2 \* 3)) == 7) and true)' "$tmp/expression_valid.suru.out"
grep -q '^expr|items\[0\]\.name(first,(second + 1))' \
    "$tmp/expression_postfix_valid.suru.out"
grep -q '^expr|(((1 + (2 \* 3)) == 7) ? (a ? b : c) : d)' \
    "$tmp/expression_ternary_valid.suru.out"

for input in expression_invalid.suru expression_unclosed.suru; do
    if "$SURU" run "$DIR/natural_expression_driver.suru" \
           "$ROOT/tests/natural/$input" >"$tmp/$input.out"; then
        echo "natural expression parser: $input was accepted" >&2
        exit 1
    fi
    grep -q '^expr-error|' "$tmp/$input.out"
done

"$SURU" run "$DIR/natural_expression_ast_driver.suru" \
    "$ROOT/tests/natural/expression_valid.suru" >"$tmp/typed-ast.out"
grep -q '^expr-root|8
 "$tmp/typed-ast.out"
grep -q '^expr-node|3|binary|\\*|1,2
 "$tmp/typed-ast.out"
grep -q '^expr-node|4|binary|+|0,3
 "$tmp/typed-ast.out"
grep -q '^expr-node|6|binary|==|4,5
 "$tmp/typed-ast.out"
grep -q '^expr-node|8|binary|and|6,7
 "$tmp/typed-ast.out"

echo "natural expression parser: precedence and postfix corpus passed"
