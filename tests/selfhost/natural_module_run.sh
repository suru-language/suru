#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_module_driver.suru" \
    "$ROOT/tests/natural/module_main.suru" >"$tmp/valid.out"
test ! -s "$tmp/valid.out"

for input in module_cycle_a module_missing; do
    if "$SURU" run "$DIR/natural_module_driver.suru" \
           "$ROOT/tests/natural/$input.suru" >"$tmp/$input.out"; then
        echo "natural modules: $input was accepted" >&2
        exit 1
    fi
    grep -q '^module-error|' "$tmp/$input.out"
done

echo "natural modules: relative resolution and cycle corpus passed"
