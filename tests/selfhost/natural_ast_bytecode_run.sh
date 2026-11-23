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
range_expected="$(printf '6\n1\n2\n3\n9223372036854775807')"
if [ "$range_actual" != "$range_expected" ]; then
    echo "AST each-range execution mismatch: '$range_actual'" >&2
    exit 1
fi
indexed_source="$ROOT/tests/natural/routine_ast_index_assignment.suru"
"$SURU" --syntax=natural compile "$indexed_source" "$tmp/reference-index.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$indexed_source" "$tmp/ast-index.sbc"
if ! cmp -s "$tmp/reference-index.sbc" "$tmp/ast-index.sbc"; then
    echo "AST indexed-assignment bytecode differs from reference" >&2
    cmp -l "$tmp/reference-index.sbc" "$tmp/ast-index.sbc" | head -12 >&2 || true
    exit 1
fi
indexed_actual="$("$SURU" exec "$tmp/ast-index.sbc")"
indexed_expected="$(printf '2\n9')"
if [ "$indexed_actual" != "$indexed_expected" ]; then
    echo "AST indexed-assignment execution mismatch: '$indexed_actual'" >&2
    exit 1
fi
across_source="$DIR/natural_ast_each_across.suru"
echo "natural AST bytecode: compiling each-across fixture"
timeout -k 1s 5s "$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$across_source" "$tmp/ast-across.sbc"
echo "natural AST bytecode: executing each-across module"
across_actual="$(timeout -k 1s 5s "$SURU" exec "$tmp/ast-across.sbc")"
across_expected="$(printf '4\n5\n6\n15')"
if [ "$across_actual" != "$across_expected" ]; then
    echo "AST each-across execution mismatch: '$across_actual'" >&2
    exit 1
fi
echo "natural AST bytecode: each-across array iteration emitted and executed with the expected values"
echo "natural AST bytecode: exclusive/inclusive ranges, indexed writes, predeclared calls, nested control flow, and routine output passed"
