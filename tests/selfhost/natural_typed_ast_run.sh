#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_typed_ast_module_driver.suru" \
    "$ROOT/tests/natural/ast_statements_valid.suru" >"$tmp/statements.out"
grep -F -q 'typed-node|0|binding|1|4' "$tmp/statements.out"
grep -F -q 'typed-node|2|assignment|3|6,9' "$tmp/statements.out"
grep -F -q 'typed-expr|3|binary|*|1,2' "$tmp/statements.out"
grep -F -q 'typed-expr|4|binary|+|0,3' "$tmp/statements.out"
grep -F -q 'typed-expr|9|binary|+|7,8' "$tmp/statements.out"

"$SURU" run "$DIR/natural_typed_ast_module_driver.suru" \
    "$ROOT/tests/natural/control_ast_valid.suru" >"$tmp/control.out"
grep -F -q 'typed-node|0|repeat|1|2' "$tmp/control.out"
grep -F -q 'typed-expr|2|binary|<|0,1' "$tmp/control.out"
grep -F -q 'typed-node|2|when|4|5' "$tmp/control.out"
grep -F -q 'typed-node|9|each|11|12' "$tmp/control.out"
grep -F -q 'typed-expr|20|call||18,19' "$tmp/control.out"

if "$SURU" run "$DIR/natural_typed_ast_module_driver.suru" \
       "$ROOT/tests/natural/ast_statement_invalid.suru" >"$tmp/invalid.out"; then
    echo "typed AST accepted an invalid statement" >&2
    exit 1
fi
grep -q '^ast-error|' "$tmp/invalid.out"

echo "natural typed AST: statement and control expressions attached to module AST"
