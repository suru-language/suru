#!/usr/bin/env bash
# Verifier gate: natural_verifier.suru must accept every whole-program fixture
# the C compiler accepts (mirroring src/verify.c), and must reject injected
# invalid modules (bad opcode, out-of-bounds constants/branches, stack
# underflow, missing terminal).
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

fail=0
tests=0

# Positive: programs that pass the C verifier (runnable whole programs).
for input in \
    tests/natural/control_valid.suru \
    tests/natural/bytecode_values.suru \
    tests/natural/bytecode_ternary.suru \
    tests/natural/bytecode_effects.suru \
    tests/natural/record_choice_valid.suru \
    tests/natural/effects_valid.suru \
    tests/natural/operators_valid.suru \
    tests/natural/numeric_valid.suru \
    tests/natural/expression_postfix_valid.suru \
    tests/natural/modules_valid.suru \
    tests/natural/module_main.suru; do
    tests=$((tests + 1))
    if ! "$SURU" run "$DIR/natural_verify_driver.suru" "$ROOT/$input" 2>/dev/null; then
        echo "FAIL: verifier rejected $(basename "$input")" >&2
        fail=$((fail + 1))
    fi
done

# Negative: injected invalid modules must be rejected.
if ! "$SURU" run "$DIR/natural_verify_cases_driver.suru" 2>/dev/null; then
    echo "FAIL: verifier did not reject invalid module(s)" >&2
    fail=$((fail + 1))
fi
tests=$((tests + 1))

echo "natural verifier: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
