#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_semantic_driver.suru" \
    "$ROOT/tests/natural/semantic_valid.suru" >"$tmp/valid.out"
test ! -s "$tmp/valid.out"
"$SURU" run "$DIR/natural_semantic_driver.suru" \
    "$ROOT/tests/natural/semantic_ternary_valid.suru" >"$tmp/ternary-valid.out"
test ! -s "$tmp/ternary-valid.out"

for input in semantic_invalid_type semantic_invalid_set semantic_invalid_condition \
             semantic_invalid_ternary; do
    if "$SURU" run "$DIR/natural_semantic_driver.suru" \
           "$ROOT/tests/natural/$input.suru" >"$tmp/$input.out"; then
        echo "natural semantic pass: $input was accepted" >&2
        exit 1
    fi
    grep -q '^semantic-error|' "$tmp/$input.out"
done

echo "natural semantic pass: type and mutability corpus passed"
