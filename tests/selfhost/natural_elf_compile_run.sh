#!/usr/bin/env bash
# Native-backend seed gate: the minimal "Natural -> standalone ELF" path
# (natural_elf_compile_driver.suru) must compile a trivial `show("literal")`
# program into an x86-64 ELF64 executable that prints the literal.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

compile_and_run() {
    local name="$1"
    local src="$2"
    local want="$3"
    printf '%b\n' "$src" > "$tmp/$name.suru"
    if ! "$SURU" run "$DIR/natural_elf_compile_driver.suru" \
            "$tmp/$name.suru" "$tmp/$name.elf" 2>/dev/null; then
        echo "FAIL: native compile failed for $name" >&2
        return 1
    fi
    if ! file "$tmp/$name.elf" | grep -q "ELF 64-bit LSB executable, x86-64"; then
        echo "FAIL: $name output is not an x86-64 ELF64 executable" >&2
        return 1
    fi
    chmod +x "$tmp/$name.elf"
    out="$("$tmp/$name.elf")"
    rc=$?
    if [ "$rc" -ne 0 ] || [ "$out" != "$want" ]; then
        echo "FAIL: $name ran rc=$rc out='$out' want='$want'" >&2
        return 1
    fi
    return 0
}

fail=0
tests=0

if compile_and_run str 'routine main() -> Unit\n    show("hello-native")\nend routine' "hello-native"; then
    tests=$((tests + 1))
else
    fail=$((fail + 1))
fi
if compile_and_run int 'routine main() -> Unit\n    show(123)\nend routine' "123"; then
    tests=$((tests + 1))
else
    fail=$((fail + 1))
fi
if compile_and_run zero 'routine main() -> Unit\n    show(0)\nend routine' "0"; then
    tests=$((tests + 1))
else
    fail=$((fail + 1))
fi
if compile_and_run arith 'routine main() -> Unit\n    show(1 + 2 * 3)\nend routine' "7"; then
    tests=$((tests + 1))
else
    fail=$((fail + 1))
fi

echo "natural ELF compile: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
