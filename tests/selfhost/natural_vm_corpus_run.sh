#!/usr/bin/env bash
# Broad VM-parity gate: for every fixture the C compiler accepts AND the C17 VM
# executes cleanly (no trap), the SURU interpreter (natural_vm.suru) must
# produce exactly the same output.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail=0
tests=0
for input in "$ROOT"/tests/natural/*.suru; do
    if ! "$SURU" --syntax=natural compile "$input" "$tmp/x.sbc" 2>/dev/null; then
        continue
    fi
    if ! "$SURU" exec "$tmp/x.sbc" > "$tmp/c.out" 2>/dev/null; then
        continue
    fi
    tests=$((tests + 1))
    if ! "$SURU" run "$DIR/natural_vm_driver.suru" \
            "$tmp/x.sbc" > "$tmp/v.out" 2>/dev/null; then
        echo "FAIL: SURU VM crashed on $(basename "$input")" >&2
        fail=$((fail + 1))
        continue
    fi
    if ! cmp -s "$tmp/c.out" "$tmp/v.out"; then
        echo "FAIL: VM output differs from C on $(basename "$input")" >&2
        fail=$((fail + 1))
    fi
done

echo "natural VM corpus parity: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
