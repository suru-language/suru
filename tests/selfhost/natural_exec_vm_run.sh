#!/usr/bin/env bash
# CLI integration gate: `suru exec --vm=natural` (SURU-written interpreter) must
# produce the same output and exit status as the default C17 exec for modules the
# reference VM runs cleanly.
set -eu

SURU="${1:-build/bin/suru}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail=0
tests=0

check_module() {
    local name="$1"
    local file="$2"
    tests=$((tests + 1))
    local c v
    c=$("$SURU" exec "$file" 2>&1) || { echo "FAIL $name: reference exec failed" >&2; fail=$((fail + 1)); return; }
    v=$("$SURU" exec --vm=natural "$file" 2>&1) || { echo "FAIL $name: --vm=natural failed" >&2; fail=$((fail + 1)); return; }
    if [ "$v" != "$c" ]; then
        echo "FAIL $name: C=[$c] naturalVM=[$v]" >&2
        fail=$((fail + 1))
    fi
}

printf 'routine main() -> Unit\n    show("hi")\nend routine\n' > "$tmp/hello.suru"
"$SURU" --syntax=natural compile "$tmp/hello.suru" "$tmp/hello.sbc"
"$SURU" --syntax=natural compile "$ROOT/tests/natural/control_valid.suru" "$tmp/control_valid.sbc"
"$SURU" --syntax=natural compile "$ROOT/tests/natural/bytecode_effects.suru" "$tmp/bytecode_effects.sbc"

check_module hello "$tmp/hello.sbc"
check_module control_valid "$tmp/control_valid.sbc"
check_module bytecode_effects "$tmp/bytecode_effects.sbc"

echo "natural exec --vm=natural: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
