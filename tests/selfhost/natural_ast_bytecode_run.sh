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
echo "natural AST bytecode: routine/binding/call/show/give output matched reference"
