#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

check_type() {
    expression="$1"
    expected="$2"
    printf '%s\n' "$expression" > "$tmp/expression.suru"
    "$SURU" run "$DIR/natural_expression_types_driver.suru" \
        "$tmp/expression.suru" > "$tmp/result.out"
    grep -F -q "type|$expected" "$tmp/result.out"
}

check_error() {
    expression="$1"
    printf '%s\n' "$expression" > "$tmp/expression.suru"
    if "$SURU" run "$DIR/natural_expression_types_driver.suru" \
           "$tmp/expression.suru" > "$tmp/result.out"; then
        echo "expression type checker accepted invalid expression: $expression" >&2
        exit 1
    fi
    grep -q '^type-error|' "$tmp/result.out"
}

check_type 'count + 2 * 3' 'Whole'
check_type 'count + ratio' 'Real'
check_type 'ready and (count < ratio)' 'Bool'
check_type '"hello " + label' 'Text'
check_type 'count > 0 ? count : 1' 'Whole'
check_type '[count, 2]' '[Whole]'
check_type '[count, ratio]' '[Real]'
check_type 'items[0]' 'Whole'
check_type 'point.x' 'Whole'
check_type 'point.name' 'Text'
check_type 'str_len(label)' 'Whole'
check_type 'substr(label, 0, 1)' 'Text'
check_type 'str_contains(label, "x")' 'Bool'
check_type 'range(0, count)' '[Whole]'
check_type 'array_concat(items, [count])' '[Whole]'
check_type 'slice(items, 0, 1)' '[Whole]'

check_error 'count + label'
check_error 'ready + count'
check_error 'count and ready'
check_error 'missing'
check_error '[count, label]'
check_error 'label[ready]'
check_error 'str_len(count)'
check_error 'count ? label : label'
check_error 'count > 0 ? label : count'

echo "natural expression typing: AST-based inference and rejection corpus passed"
