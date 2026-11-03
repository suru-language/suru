#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for input in control_valid.suru record_choice_valid.suru \
             modules_valid.suru keywords_valid.suru; do
    "$SURU" run "$DIR/natural_parser_driver.suru" \
        "$ROOT/tests/natural/$input" >"$tmp/$input.out"
done

if "$SURU" run "$DIR/natural_parser_driver.suru" \
       "$ROOT/tests/natural/invalid_terminator.suru" \
       >"$tmp/invalid.out"; then
    echo "natural parser: mismatched terminator was accepted" >&2
    exit 1
fi
grep -q '^parse-error|301|' "$tmp/invalid.out"

for case in invalid_assignment:313 invalid_binding:307 \
            invalid_public_type:309 invalid_parameter_type:308 \
            invalid_const_scope:312 invalid_return:311 invalid_give:310; do
    input="${case%%:*}"
    code="${case##*:}"
    if "$SURU" run "$DIR/natural_parser_driver.suru" \
           "$ROOT/tests/natural/$input.suru" >"$tmp/$input.out"; then
        echo "natural parser: $input was accepted" >&2
        exit 1
    fi
    grep -q "^parse-error|$code|" "$tmp/$input.out"
done

echo "natural parser: structural corpus passed"
