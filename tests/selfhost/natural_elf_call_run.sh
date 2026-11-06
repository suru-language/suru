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
if check_call twopar 'routine add(a: Whole, b: Whole) -> Whole\n    give a + b\nend routine\nroutine main() -> Unit\n    show(add(3, 4))\nend routine' "7"; then :; else fail=$((fail + 1)); fi
if check_call recursive 'routine fib(n: Whole) -> Whole\n    when n < 2\n        give n\n    otherwise\n        give fib(n - 1) + fib(n - 2)\n    end when\nend routine\nroutine main() -> Unit\n    show(to_str(fib(6)))\nend routine' "8"; then :; else fail=$((fail + 1)); fi
if check_call sum5 'routine sum5(a: Whole, b: Whole, c: Whole, d: Whole, e: Whole) -> Whole\n    give a + b + c + d + e\nend routine\nroutine main() -> Unit\n    show(sum5(1, 2, 3, 4, 5))\nend routine' "15"; then :; else fail=$((fail + 1)); fi
if check_call sum6 'routine sum6(a: Whole, b: Whole, c: Whole, d: Whole, e: Whole, f: Whole) -> Whole\n    give a + b + c + d + e + f\nend routine\nroutine main() -> Unit\n    show(sum6(1, 2, 3, 4, 5, 6))\nend routine' "21"; then :; else fail=$((fail + 1)); fi

echo "natural ELF call: 6 tests, $fail failures"
[ "$fail" -eq 0 ]
