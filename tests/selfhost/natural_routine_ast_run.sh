#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_ast_module_driver.suru" \
    "$ROOT/tests/natural/routine_ast_valid.suru" > "$tmp/valid.out"
grep -F -q 'ast|routine|1|add|a:Whole,b:Whole|Whole|' "$tmp/valid.out"
grep -F -q 'ast|routine|5|main||Unit|' "$tmp/valid.out"
grep -F -q 'ast|end-routine|4|' "$tmp/valid.out"
grep -F -q 'ast|end-routine|7|' "$tmp/valid.out"

for input in routine_ast_invalid_parameter.suru routine_ast_mismatched_end.suru; do
    if "$SURU" run "$DIR/natural_ast_module_driver.suru" \
           "$ROOT/tests/natural/$input" > "$tmp/$input.out"; then
        echo "routine AST parser accepted invalid fixture: $input" >&2
        exit 1
    fi
    grep -q '^ast-error\|^control-error' "$tmp/$input.out"
done

echo "natural module AST: routine signatures and body boundaries passed"
