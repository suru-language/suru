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
    printf '%b\n' "$src" > "$tmp/$name.suru"
    if ! "$SURU" --syntax=natural compile "$tmp/$name.suru" \
            "$tmp/$name.sbc" 2>/dev/null; then
        return
    fi
    tests=$((tests + 1))
    if ! c=$("$SURU" exec "$tmp/$name.sbc" 2>&1); then
        echo "FAIL $name: reference VM failed" >&2
        fail=$((fail + 1))
        return
    fi
    if ! s=$("$SURU" run "$DIR/natural_vm_driver.suru" \
            "$tmp/$name.sbc" 2>&1); then
        echo "FAIL $name: SURU VM failed" >&2
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
run_case floatadd 'routine main() -> Unit\n    show(1.5 + 2.5)\nend routine'
run_case floatmul 'routine main() -> Unit\n    show(2.0 * 3.0)\nend routine'
run_case cmp 'routine main() -> Unit\n    show(3 < 5)\n    show(5 >= 5)\nend routine'
run_case loop 'routine main() -> Unit\n    value i: Whole = 0\n    repeat while i < 3\n        show(to_str(i))\n        set i to i + 1\n    end repeat\nend routine'
run_case when 'routine main() -> Unit\n    value x: Whole = 2\n    when x > 1\n        show("big")\n    otherwise\n        show("small")\n    end when\nend routine'
run_case call 'routine double(x: Whole) -> Whole\n    give x * 2\nend routine\nroutine main() -> Unit\n    value a: Whole = double(5)\n    show(to_str(a))\n    show(to_str(double(21)))\nend routine'
run_case fib 'routine fib(n: Whole) -> Whole\n    when n < 2\n        give n\n    otherwise\n        give fib(n - 1) + fib(n - 2)\n    end when\nend routine\nroutine main() -> Unit\n    show(to_str(fib(6)))\nend routine'
run_case arr 'routine main() -> Unit\n    value xs: [Whole] = [10, 20, 30]\n    show(xs[1])\nend routine'
run_case arrset 'routine main() -> Unit\n    value xs: [Whole] = [1, 2]\n    set xs[0] to 9\n    show(xs[0])\nend routine'
run_case rec 'record Point\n    change x: Whole\n    change y: Whole\nend record\nroutine main() -> Unit\n    value p: Point = none\n    set p.x to 3\n    set p.y to 4\n    show(p.x + p.y)\nend routine'
run_case recparam 'record Point\n    change x: Whole\nend record\nroutine f(n: Whole, p: Point) -> Whole\n    give p.x + n\nend routine\nroutine main() -> Unit\n    value p: Point = none\n    set p.x to 5\n    show(f(2, p))\nend routine'

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
