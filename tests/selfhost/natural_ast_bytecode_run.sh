#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
source="$ROOT/tests/natural/routine_ast_bytecode.suru"
"$SURU" --syntax=natural compile "$source" "$tmp/reference.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$source" "$tmp/ast.sbc"
if ! cmp -s "$tmp/reference.sbc" "$tmp/ast.sbc"; then
    echo "AST bytecode differs from reference:" >&2
    cmp -l "$tmp/reference.sbc" "$tmp/ast.sbc" | head -12 >&2 || true
    echo "reference:" >&2
    od -An -tx1 -N128 "$tmp/reference.sbc" >&2
    echo "AST:" >&2
    od -An -tx1 -N128 "$tmp/ast.sbc" >&2
    exit 1
fi
actual="$("$SURU" exec "$tmp/ast.sbc")"
expected="$(printf '5\n5')"
if [ "$actual" != "$expected" ]; then
    echo "AST bytecode execution mismatch: '$actual'" >&2
    exit 1
fi
range_source="$ROOT/tests/natural/routine_ast_each_range.suru"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$range_source" "$tmp/ast-each-range.sbc"
range_actual="$("$SURU" exec "$tmp/ast-each-range.sbc")"
range_expected="$(printf '6\n1\n2\n3')"
if [ "$range_actual" != "$range_expected" ]; then
    echo "AST each-range execution mismatch: '$range_actual'" >&2
    exit 1
fi
echo "natural AST bytecode: exclusive until and inclusive through range loops executed correctly"
echo "natural AST bytecode: AST-predeclared forward routine calls, bindings, nested when/otherwise, repeat, show and give matched reference"
