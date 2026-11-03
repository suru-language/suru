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

echo "natural parser: structural corpus passed"
