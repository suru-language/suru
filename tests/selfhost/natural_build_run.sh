#!/usr/bin/env bash
# Full-compiler gate: natural_build_driver.suru (frontend -> bytecode ->
# verifier -> module writer) must accept only programs that pass the entire
# SURU pipeline, and every accepted program must produce a .sbc byte-identical
# to the C reassembly path, runnable on the C VM.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail=0
tests=0
accepted=0
for input in "$ROOT"/tests/natural/*.suru; do
    if ! "$SURU" --syntax=natural compile "$input" "$tmp/c.sbc" 2>/dev/null; then
        continue
    fi
    if "$SURU" run "$DIR/natural_build_driver.suru" \
            "$input" "$tmp/b.sbc" >/dev/null 2>&1; then
        accepted=$((accepted + 1))
        tests=$((tests + 1))
        if ! cmp -s "$tmp/c.sbc" "$tmp/b.sbc"; then
            echo "FAIL: build output differs from C on $(basename "$input")" >&2
            echo "  C bytes: $(wc -c < "$tmp/c.sbc"), SURU bytes: $(wc -c < "$tmp/b.sbc")" >&2
            echo "  first differing offsets (1-based):" >&2
            cmp -l "$tmp/c.sbc" "$tmp/b.sbc" | head -20 >&2 || true
            "$SURU" run "$DIR/natural_bytecode_driver.suru" "$input" >"$tmp/c.ir" 2>/dev/null || true
            "$SURU" run "$DIR/natural_bytecode_after_frontend_driver.suru" "$input" >"$tmp/b.ir" 2>/dev/null || true
            echo "  bytecode IR diff:" >&2
            diff -u "$tmp/c.ir" "$tmp/b.ir" | head -60 >&2 || true
            "$SURU" run "$DIR/natural_bytecode_raw_driver.suru" "$input" >"$tmp/c.raw" 2>/dev/null || true
            "$SURU" run "$DIR/natural_bytecode_raw_after_frontend_driver.suru" "$input" >"$tmp/b.raw" 2>/dev/null || true
            echo "  raw instruction diff:" >&2
            diff -u "$tmp/c.raw" "$tmp/b.raw" | head -80 >&2 || true
            fail=$((fail + 1))
        fi
    fi
done

printf 'routine main() -> Unit\n    value x: Real = 1.5\n    value y: Real = 2.5\n    show(x + y)\nend routine\n' \
    > "$tmp/float.suru"
"$SURU" run "$DIR/natural_build_driver.suru" \
    "$tmp/float.suru" "$tmp/float.sbc" >/dev/null 2>&1
actual="$("$SURU" --syntax=natural run "$tmp/float.suru" 2>/dev/null)"
runout="$("$SURU" exec "$tmp/float.sbc" 2>/dev/null)"
tests=$((tests + 2))
if [ "$actual" != "4" ] || [ "$runout" != "4" ]; then
    echo "FAIL: build driver exec parity (run='$actual' exec='$runout')" >&2
    fail=$((fail + 1))
fi

# A parameter name reused from another routine must not inherit its
# earlier global slot. The second routine deliberately has a different first
# parameter so a leaked slot id would reverse the parameter-to-local mapping.
"$SURU" --syntax=natural compile "$ROOT/tests/natural/per_function_slots.suru" \
    "$tmp/per-function-c.sbc"
"$SURU" run "$DIR/natural_build_driver.suru" \
    "$ROOT/tests/natural/per_function_slots.suru" "$tmp/per-function-self.sbc" \
    >/dev/null 2>&1
cmp "$tmp/per-function-c.sbc" "$tmp/per-function-self.sbc"
actual="$("$SURU" exec "$tmp/per-function-self.sbc" 2>/dev/null)"
if [ "$actual" != "304" ]; then
    echo "FAIL: per-function slot isolation (got '$actual', want '304')" >&2
    fail=$((fail + 1))
else
    tests=$((tests + 1))
fi

echo "natural build driver: accepted=$accepted, $tests tests, $fail failures"
[ "$fail" -eq 0 ]
