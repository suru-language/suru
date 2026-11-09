#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_control_driver.suru" \
    "$ROOT/tests/natural/control_ast_valid.suru" >"$tmp/valid.out"
"$SURU" run "$DIR/natural_control_driver.suru" \
    "$ROOT/tests/natural/control_each_through_valid.suru" >"$tmp/each-through.out"
grep -F -q 'ast|each|1|n|from|1|through|5' "$tmp/each-through.out"
grep -F -q 'ast|end-each|3|' "$tmp/each-through.out"
grep -F -q 'ast|repeat|1|(attempts < 3)' "$tmp/valid.out"
grep -F -q 'ast|when|4|(attempts == 3)' "$tmp/valid.out"
grep -F -q 'ast|otherwise-when|6|(attempts > 3)' "$tmp/valid.out"
grep -F -q 'ast|otherwise|8|' "$tmp/valid.out"
grep -F -q 'ast|each|11|item|across|items' "$tmp/valid.out"
grep -F -q 'ast|choose|17|state' "$tmp/valid.out"
grep -F -q 'ast|case|18|Connected(address)' "$tmp/valid.out"
grep -F -q 'ast|end-choose|22|' "$tmp/valid.out"

if "$SURU" run "$DIR/natural_control_driver.suru" \
       "$ROOT/tests/natural/control_ast_invalid.suru" >"$tmp/invalid.out"; then
    echo "natural control parser: mismatched terminator was accepted" >&2
    exit 1
fi
grep -q '^control-error|3|mismatched block terminator$' "$tmp/invalid.out"

echo "natural control AST parser: block corpus passed"
