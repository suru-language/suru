#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_declaration_driver.suru" \
    "$ROOT/tests/natural/declarations_semantic_valid.suru" >"$tmp/valid.out"

for input in declarations_invalid_record declarations_invalid_choice \
             declarations_invalid_module declarations_invalid_effect \
             declarations_invalid_exhaustive declarations_invalid_contract; do
    if "$SURU" run "$DIR/natural_declaration_driver.suru" \
           "$ROOT/tests/natural/$input.suru" >"$tmp/$input.out"; then
        echo "natural declaration semantics: $input was accepted" >&2
        exit 1
    fi
    grep -q '^declaration-error|' "$tmp/$input.out"
done

echo "natural declaration semantics: records, choices, modules, routines, and effects passed"
