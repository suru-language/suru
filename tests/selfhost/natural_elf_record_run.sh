#!/usr/bin/env bash
# Native record gate: record construction + field write/read must compile to an
# x86-64 ELF64 executable, and arrays+records must coexist on the same bump.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

check_rec() {
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
if check_rec rec 'record Point\n    change x: Whole\nend record\nroutine main() -> Unit\n    value p: Point = none\n    set p.x to 5\n    show(p.x)\nend routine' "5"; then :; else fail=$((fail + 1)); fi
if check_rec mix 'record Point\n    change x: Whole\nend record\nroutine main() -> Unit\n    value xs: [Whole] = [10, 20, 30]\n    value p: Point = none\n    set p.x to 5\n    show(xs[1] + p.x)\nend routine' "25"; then :; else fail=$((fail + 1)); fi
if check_rec twoarr 'routine main() -> Unit\n    value a: [Whole] = [1, 2]\n    value b: [Whole] = [9, 8, 7]\n    show(a[0] + b[2])\nend routine' "8"; then :; else fail=$((fail + 1)); fi

echo "natural ELF record: 3 tests, $fail failures"
[ "$fail" -eq 0 ]
