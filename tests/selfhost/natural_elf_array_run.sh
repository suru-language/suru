#!/usr/bin/env bash
# Native array gate: integer array literal + index read must compile to an
# x86-64 ELF64 executable that prints the indexed element.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

check_arr() {
    local name="$1"
    local idx="$2"
    local want="$3"
    cat > "$tmp/$name.suru" <<EOF
routine main() -> Unit
    value xs: [Whole] = [10, 20, 30]
    show(xs[$idx])
end routine
EOF
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
if check_arr a0 0 10; then :; else fail=$((fail + 1)); fi
if check_arr a1 1 20; then :; else fail=$((fail + 1)); fi
if check_arr a2 2 30; then :; else fail=$((fail + 1)); fi

echo "natural ELF array: 3 tests, $fail failures"
[ "$fail" -eq 0 ]
