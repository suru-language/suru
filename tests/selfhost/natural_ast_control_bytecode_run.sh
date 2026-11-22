#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
source="$ROOT/tests/natural/ast_bytecode_control.suru"
"$SURU" --syntax=natural compile "$source" "$tmp/reference.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$source" "$tmp/ast.sbc"
cmp "$tmp/reference.sbc" "$tmp/ast.sbc"
actual="$("$SURU" exec "$tmp/ast.sbc")"
expected="$(printf 'yes\n2\n3\n')"
if [ "$actual" != "$expected" ]; then
    echo "AST control bytecode mismatch: got '$actual'" >&2
    exit 1
fi
echo "natural AST bytecode: when/otherwise and repeat while matched reference"
