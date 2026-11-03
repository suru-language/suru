#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for input in expression_valid.suru expression_postfix_valid.suru; do
    "$SURU" run "$DIR/natural_expression_driver.suru" \
        "$ROOT/tests/natural/$input" >"$tmp/$input.out"
    grep -q '^expr|' "$tmp/$input.out"
done

grep -q '^expr|(1 + (2 * 3)) == 7 and true' "$tmp/expression_valid.suru.out"
grep -q '^expr|items\[0\]\.name(first,(second + 1))' \
    "$tmp/expression_postfix_valid.suru.out"

for input in expression_invalid.suru expression_unclosed.suru; do
    if "$SURU" run "$DIR/natural_expression_driver.suru" \
           "$ROOT/tests/natural/$input" >"$tmp/$input.out"; then
        echo "natural expression parser: $input was accepted" >&2
        exit 1
    fi
    grep -q '^expr-error|' "$tmp/$input.out"
done

echo "natural expression parser: precedence and postfix corpus passed"
