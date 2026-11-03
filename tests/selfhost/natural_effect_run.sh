#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_effect_driver.suru" \
    "$ROOT/tests/natural/effects_valid.suru" >"$tmp/valid.out"
test ! -s "$tmp/valid.out"

for input in effects_invalid effects_invalid_handler; do
    if "$SURU" run "$DIR/natural_effect_driver.suru" \
           "$ROOT/tests/natural/$input.suru" >"$tmp/$input.out"; then
        echo "natural effects: $input was accepted" >&2
        exit 1
    fi
    grep -q '^effect-error|' "$tmp/$input.out"
done

echo "natural effects: propagation and handler corpus passed"
