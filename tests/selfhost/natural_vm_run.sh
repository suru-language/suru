#!/usr/bin/env bash
# SURU VM gate: natural_vm.suru (via natural_vm_driver.suru) must execute
# .sbc modules and produce output byte-identical to the C17 VM for the corpus
# programs that run cleanly on the reference VM.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail=0
tests=0

run_case() {
    local name="$1"
    local src="$2"
    local required="${3:-optional}"
    printf '%b\n' "$src" > "$tmp/$name.suru"
    if ! "$SURU" --syntax=natural compile "$tmp/$name.suru" \
            "$tmp/$name.sbc" 2>"$tmp/$name-compile.err"; then
        if [ "$required" = "required" ]; then
            echo "FAIL $name: required fixture did not compile" >&2
            cat "$tmp/$name-compile.err" >&2
            fail=$((fail + 1))
        fi
        return
    fi
    tests=$((tests + 1))
    if ! c=$("$SURU" exec "$tmp/$name.sbc" 2>&1); then
        echo "FAIL $name: reference VM failed: [$c]" >&2
        fail=$((fail + 1))
        return
    fi
    if ! s=$("$SURU" run "$DIR/natural_vm_driver.suru" \
            "$tmp/$name.sbc" 2>&1); then
        echo "FAIL $name: SURU VM failed: [$s]" >&2
        fail=$((fail + 1))
        return
    fi
    if [ "$s" != "$c" ]; then
        echo "FAIL $name: C=[$c] SURU=[$s]" >&2
        fail=$((fail + 1))
    fi
}

run_case hello 'routine main() -> Unit\n    show("hi")\nend routine'
run_case arith 'routine main() -> Unit\n    show(to_str(1 + 2 * 3))\n    show(to_str(9 - 4))\nend routine'
run_case neg 'routine main() -> Unit\n    show(to_str(0 - 5))\nend routine'
run_case concat 'routine main() -> Unit\n    show("foo" + "bar")\nend routine'
run_case strcmp 'routine main() -> Unit\n    show("a" < "b")\n    show("same" == "same")\n    show("z" >= "a")\nend routine'
run_case strbuiltins 'routine main() -> Unit\n    show(str_len("hello"))\n    show(substr("hello", 1, 3))\n    show(str_contains("alphabet", "pha"))\nend routine'
run_case tostring 'routine main() -> Unit\n    show(to_str(42))\n    show(to_str(1.5))\nend routine'
run_case floatadd 'routine main() -> Unit\n    show(1.5 + 2.5)\nend routine'
run_case floatmul 'routine main() -> Unit\n    show(2.0 * 3.0)\nend routine'
run_case floatneg 'routine negate(value: Real) -> Real\n    give -value\nend routine\nroutine main() -> Unit\n    show(negate(1.5))\n    show(negate(0.0))\nend routine' required
run_case floatcmp 'routine compare(left: Real, right: Real) -> Unit\n    show(left == right)\n    show(left != right)\n    show(left < right)\n    show(left <= right)\n    show(left > right)\n    show(left >= right)\nend routine\nroutine main() -> Unit\n    compare(1.5, 2.5)\nend routine' required
run_case bitwise 'routine bit_ops(left: Whole, right: Whole) -> Unit\n    show(left & right)\n    show(left | right)\n    show(left ^ right)\n    show(left << right)\n    show(left >> right)\n    show(~left)\nend routine\nroutine shift(left: Whole, count: Whole) -> Whole\n    give left >> count\nend routine\nroutine shift_left(left: Whole, count: Whole) -> Whole\n    give left << count\nend routine\nroutine main() -> Unit\n    bit_ops(12, 2)\n    show(shift(-8, 1))\n    show(shift(8, 64))\n    show(shift(8, -1))\n    show(shift_left(3, 64))\nend routine' required
run_case boolcmp 'routine invert(value: Bool) -> Bool\n    give not value\nend routine\nroutine compare(left: Bool, right: Bool) -> Unit\n    show(invert(left) == invert(right))\n    show(invert(left) != invert(right))\nend routine\nroutine main() -> Unit\n    compare(true, true)\n    compare(true, false)\nend routine' required
run_case cmp 'routine main() -> Unit\n    show(3 < 5)\n    show(5 >= 5)\nend routine'
run_case loop 'routine main() -> Unit\n    value i: Whole = 0\n    repeat while i < 3\n        show(to_str(i))\n        set i to i + 1\n    end repeat\nend routine'
run_case when 'routine main() -> Unit\n    value x: Whole = 2\n    when x > 1\n        show("big")\n    otherwise\n        show("small")\n    end when\nend routine'
run_case call 'routine double(x: Whole) -> Whole\n    give x * 2\nend routine\nroutine main() -> Unit\n    value a: Whole = double(5)\n    show(to_str(a))\n    show(to_str(double(21)))\nend routine'
run_case fib 'routine fib(n: Whole) -> Whole\n    when n < 2\n        give n\n    otherwise\n        give fib(n - 1) + fib(n - 2)\n    end when\nend routine\nroutine main() -> Unit\n    show(to_str(fib(6)))\nend routine'
run_case arr 'routine main() -> Unit\n    value xs: [Whole] = [10, 20, 30]\n    show(xs[1])\nend routine'
run_case arrset 'routine main() -> Unit\n    value xs: [Whole] = [1, 2]\n    set xs[0] to 9\n    show(xs[0])\nend routine'
run_case aliasarr 'routine main() -> Unit\n    value xs: [Whole] = [1, 2]\n    value alias: [Whole] = xs\n    set xs[0] to 9\n    show(alias[0])\nend routine'
run_case nestedarr 'routine main() -> Unit\n    value inner: [Whole] = [7, 8]\n    value outer = [inner]\n    show(outer[0][1])\nend routine'
run_case recordarr 'record Holder\n    change items: [Whole]\nend record\nroutine main() -> Unit\n    value xs: [Whole] = [3, 4]\n    value box: Holder = none\n    set box.items to xs\n    show(box.items[1])\nend routine'
run_case rec 'record Point\n    change x: Whole\n    change y: Whole\nend record\nroutine main() -> Unit\n    value p: Point = none\n    set p.x to 3\n    set p.y to 4\n    show(p.x + p.y)\nend routine'
run_case recparam 'record Point\n    change x: Whole\nend record\nroutine f(n: Whole, p: Point) -> Whole\n    give p.x + n\nend routine\nroutine main() -> Unit\n    value p: Point = none\n    set p.x to 5\n    show(f(2, p))\nend routine'
run_case handled 'routine handled() -> Unit\n    attempt\n        show("try")\n    handle problem: FileError\n        show("handled")\n    end exception\n    ensure\n        show("cleanup")\n    end exception\n    give\nend attempt\nend routine'
run_case exc 'routine main() -> Unit\n    attempt\n        show("try")\n    handle e: E\n        show("handled")\n    end exception\n    ensure\n        show("cleanup")\n    end exception\n    give\nend attempt\nend routine'

# Direct v1 bytecode fixture covers operations not yet emitted by the
# natural bytecode lowerer (notably BC_CONV_I_F and BC_EQ_B / BC_NE_B).
echo "natural VM: direct scalar opcode parity"
if ! "${CC:-cc}" -std=c17 -Wall -Wextra -Wpedantic -Werror \
        -I"$ROOT/include" "$DIR/natural_vm_opcode_fixture.c" \
        "$ROOT/build/obj/bc.o" -o "$tmp/natural-vm-opcode-fixture"; then
    echo "FAIL direct scalar opcodes: fixture build failed" >&2
    fail=$((fail + 1))
elif ! "$tmp/natural-vm-opcode-fixture" "$tmp/scalar-opcodes.sbc"; then
    echo "FAIL direct scalar opcodes: fixture generation failed" >&2
    fail=$((fail + 1))
else
    tests=$((tests + 1))
    if ! c=$("$SURU" exec "$tmp/scalar-opcodes.sbc" 2>&1); then
        echo "FAIL direct scalar opcodes: reference VM failed: [$c]" >&2
        fail=$((fail + 1))
    elif ! s=$("$SURU" run "$DIR/natural_vm_driver.suru" \
            "$tmp/scalar-opcodes.sbc" 2>&1); then
        echo "FAIL direct scalar opcodes: SURU VM failed: [$s]" >&2
        fail=$((fail + 1))
    elif [ "$s" != "$c" ]; then
        echo "FAIL direct scalar opcodes: C=[$c] SURU=[$s]" >&2
        fail=$((fail + 1))
    else
        echo "natural VM: direct scalar opcode parity passed"
    fi
fi

# BC_TRAP parity: the Stage-0 compiler intentionally emits a trap for the
# array-across source-shaped fixture; both VMs must terminate with status 1.
echo "natural VM: explicit trap opcode parity"
if "$SURU" --syntax=natural compile "$DIR/natural_ast_each_across.suru" \
        "$tmp/natural-trap.sbc" 2>"$tmp/natural-trap-compile.err"; then
    tests=$((tests + 1))
    if "$SURU" exec "$tmp/natural-trap.sbc" >"$tmp/natural-trap-c.out" 2>&1; then
        c_status=0
    else
        c_status=$?
    fi
    if "$SURU" run "$DIR/natural_vm_driver.suru" \
            "$tmp/natural-trap.sbc" >"$tmp/natural-trap-suru.out" 2>&1; then
        suru_status=0
    else
        suru_status=$?
    fi
    if [ "$c_status" -ne 1 ] || [ "$suru_status" -ne 1 ]; then
        echo "FAIL BC_TRAP: C status=$c_status, SURU status=$suru_status" >&2
        cat "$tmp/natural-trap-c.out" "$tmp/natural-trap-suru.out" >&2
        fail=$((fail + 1))
    else
        echo "natural VM: explicit trap opcode passed"
    fi
else
    echo "FAIL BC_TRAP fixture did not compile:" >&2
    cat "$tmp/natural-trap-compile.err" >&2
    fail=$((fail + 1))
fi

# Fixture: control_valid.suru (runs cleanly on the reference VM).
if "$SURU" --syntax=natural compile "$ROOT/tests/natural/control_valid.suru" \
        "$tmp/control_valid.sbc" 2>/dev/null; then
    tests=$((tests + 1))
    c=$("$SURU" exec "$tmp/control_valid.sbc" 2>&1)
    s=$("$SURU" run "$DIR/natural_vm_driver.suru" \
        "$tmp/control_valid.sbc" 2>&1)
    if [ "$s" != "$c" ]; then
        echo "FAIL control_valid: C=[$c] SURU=[$s]" >&2
        fail=$((fail + 1))
    fi
fi

echo "natural VM: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
