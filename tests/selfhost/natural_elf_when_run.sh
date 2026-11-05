#!/usr/bin/env bash
# Native branch gate: `when/otherwise` over an integer comparison must compile
# to an x86-64 ELF64 executable that prints the taken branch.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

check_when() {
    local name="$1"
    local x="$2"
    local want="$3"
    cat > "$tmp/$name.suru" <<EOF
routine main() -> Unit
    value x: Whole = $x
    when x > 3
        show(111)
    otherwise
        show(222)
    end when
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
if check_when taken 5 111; then :; else fail=$((fail + 1)); fi
if check_when not 1 222; then :; else fail=$((fail + 1)); fi

echo "natural ELF when: 2 tests, $fail failures"
[ "$fail" -eq 0 ]
