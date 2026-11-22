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
if [ "$actual" != "5" ]; then
    echo "AST bytecode execution mismatch: '$actual'" >&2
    exit 1
fi
source="$ROOT/tests/natural/control_ast_bytecode.suru"
"$SURU" --syntax=natural compile "$source" "$tmp/control-reference.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$source" "$tmp/control-ast.sbc"
if ! cmp -s "$tmp/control-reference.sbc" "$tmp/control-ast.sbc"; then
    echo "AST control bytecode differs from reference:" >&2
    cmp -l "$tmp/control-reference.sbc" "$tmp/control-ast.sbc" | head -12 >&2 || true
    exit 1
fi
actual="$("$SURU" exec "$tmp/control-ast.sbc")"
if [ "$actual" != "done" ]; then
    echo "AST control bytecode execution mismatch: '$actual'" >&2
    exit 1
fi
source="$ROOT/tests/natural/each_ast_bytecode.suru"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$source" "$tmp/each-ast.sbc"
actual="$("$SURU" exec "$tmp/each-ast.sbc")"
expected="$(printf '%s\n' 12 1 2 3 1 2)"
if [ "$actual" != "$expected" ]; then
    echo "AST each bytecode execution mismatch: '$actual'" >&2
    exit 1
fi
source="$ROOT/tests/natural/array_write_ast_bytecode.suru"
"$SURU" --syntax=natural compile "$source" "$tmp/array-write-reference.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$source" "$tmp/array-write-ast.sbc"
if ! cmp -s "$tmp/array-write-reference.sbc" "$tmp/array-write-ast.sbc"; then
    echo "AST indexed assignment bytecode differs from reference:" >&2
    cmp -l "$tmp/array-write-reference.sbc" "$tmp/array-write-ast.sbc" | head -12 >&2 || true
    exit 1
fi
actual="$("$SURU" exec "$tmp/array-write-ast.sbc")"
if [ "$actual" != "9" ]; then
    echo "AST indexed assignment execution mismatch: '$actual'" >&2
    exit 1
fi
source="$ROOT/tests/natural/typed_expression_lowering.suru"
"$SURU" --syntax=natural compile "$source" "$tmp/typed-reference.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$source" "$tmp/typed-ast.sbc"
if ! cmp -s "$tmp/typed-reference.sbc" "$tmp/typed-ast.sbc"; then
    echo "AST typed-expression bytecode differs from reference:" >&2
    cmp -l "$tmp/typed-reference.sbc" "$tmp/typed-ast.sbc" | head -12 >&2 || true
    exit 1
fi
actual="$("$SURU" exec "$tmp/typed-ast.sbc")"
expected="$(printf '%s\n' 14 10 5 7 3)"
if [ "$actual" != "$expected" ]; then
    echo "AST typed-expression execution mismatch: '$actual'" >&2
    exit 1
fi
echo "natural AST bytecode: routine/binding/call/show/give output matched reference"
