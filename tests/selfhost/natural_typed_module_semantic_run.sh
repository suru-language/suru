#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

check_valid() {
    input="$1"
    "$SURU" run "$DIR/natural_typed_module_semantic_driver.suru" \
        "$ROOT/tests/natural/$input" > "$tmp/result.out"
    grep -q '^typed-module-ok|' "$tmp/result.out"
}

check_invalid() {
    input="$1"
    if "$SURU" run "$DIR/natural_typed_module_semantic_driver.suru" \
           "$ROOT/tests/natural/$input" > "$tmp/result.out"; then
        echo "module semantic checker accepted invalid fixture: $input" >&2
        exit 1
    fi
    grep -q '^semantic-error|' "$tmp/result.out"
}

check_valid typed_semantic_valid.suru
check_valid typed_semantic_array.suru
check_valid typed_semantic_builtin.suru
check_invalid typed_semantic_invalid_binding.suru
check_invalid typed_semantic_invalid_binary.suru
check_invalid typed_semantic_invalid_condition.suru
check_invalid typed_semantic_invalid_name.suru
check_invalid typed_semantic_invalid_assignment.suru

echo "natural module semantics: binding, assignment, control, array and builtin typing passed"
