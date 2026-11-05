#!/usr/bin/env bash
# Module reader gate: natural_module_reader.suru (via natural_reader_driver
# .suru) must parse every .sbc the C compiler produces and re-serialize it
# byte-for-byte identical (write -> read -> re-write round-trip).
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail=0
tests=0
for input in "$ROOT"/tests/natural/*.suru; do
    if ! "$SURU" --syntax=natural compile "$input" "$tmp/c.sbc" 2>/dev/null; then
        continue
    fi
    tests=$((tests + 1))
    if ! "$SURU" run "$DIR/natural_reader_driver.suru" \
            "$tmp/c.sbc" "$tmp/rt.sbc" 2>/dev/null; then
        echo "FAIL: reader failed on $(basename "$input")" >&2
        fail=$((fail + 1))
        continue
    fi
    if ! cmp -s "$tmp/c.sbc" "$tmp/rt.sbc"; then
        echo "FAIL: reader round-trip mismatch on $(basename "$input")" >&2
        fail=$((fail + 1))
    fi
done

echo "natural module reader: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
