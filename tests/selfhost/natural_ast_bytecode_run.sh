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
echo "natural AST bytecode: routine/binding/call/show/give output matched reference"
