#!/usr/bin/env bash
# Native call gate: a no-argument helper routine returning an integer must
# compile to an x86-64 ELF64 executable that calls it and prints the result.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

check_call() {
    local name="$1"
    local src="$2"
    local want="$3"
    printf '%b\n' "$src" > "$tmp/$name.suru"
    if ! "$SURU" run "$DIR/natural_elf_compile_driver.suru" \
            "$tmp/$name.suru" "$tmp/$name.elf" 2>/dev/null; then
        echo "FAIL: native compile failed for $name" >&2
        return 1
    fi
    chmod +x "$tmp/$name.elf"
    out="$("$tmp/$name.elf")"
    if [ "$out" != "$want" ]; then
        echo "FAIL: $name ran out='$out' want='$want'" >&2
        return 1
    fi
    return 0
}

fail=0
if check_call helper 'routine helper() -> Whole\n    give 7\nend routine\nroutine main() -> Unit\n    show(helper())\nend routine' "7"; then :; else fail=$((fail + 1)); fi
if check_call helperarith 'routine helper() -> Whole\n    give 7\nend routine\nroutine main() -> Unit\n    show(helper() + 2)\nend routine' "9"; then :; else fail=$((fail + 1)); fi

echo "natural ELF call: 2 tests, $fail failures"
[ "$fail" -eq 0 ]
