#!/usr/bin/env bash
# CLI native-backend gate: `suru --syntax=natural --backend=native compile` must
# produce an x86-64 ELF64 executable, and `run --backend=native` must match the
# default natural run for the supported subset.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail=0
tests=0

printf 'routine main() -> Unit\n    show(1 + 2 * 3)\nend routine\n' > "$tmp/a.suru"
printf 'record Point\n    change x: Whole\nend record\nroutine main() -> Unit\n    value p: Point = none\n    set p.x to 7\n    show(p.x)\nend routine\n' > "$tmp/rec.suru"

for prog in a rec; do
    tests=$((tests + 1))
    out=$("$SURU" --syntax=natural --backend=native run "$tmp/$prog.suru" 2>&1) || {
        echo "FAIL: native run failed for $prog" >&2; fail=$((fail + 1)); continue; }
    want=$("$SURU" --syntax=natural run "$tmp/$prog.suru" 2>&1)
    if [ "$out" != "$want" ]; then
        echo "FAIL: native run $prog out='$out' want='$want'" >&2
        fail=$((fail + 1))
    fi
done

tests=$((tests + 1))
if ! "$SURU" --syntax=natural --backend=native compile \
        "$tmp/a.suru" "$tmp/a.elf" 2>/dev/null; then
    echo "FAIL: native compile did not produce ELF" >&2
    fail=$((fail + 1))
elif ! file "$tmp/a.elf" | grep -q "ELF 64-bit LSB executable, x86-64"; then
    echo "FAIL: native compile output is not x86-64 ELF64" >&2
    fail=$((fail + 1))
fi

echo "natural exec --backend=native: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
