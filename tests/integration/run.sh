#!/usr/bin/env bash
# Integration test runner: for each <name>.suru, run `suru run` and compare
# stdout to <name>.out and exit code to <name>.exit (default 0).
set -u

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
fail=0
tests=0

for src in "$DIR"/*.suru; do
    [ -e "$src" ] || continue
    name="$(basename "$src" .suru)"
    [ "$name" = "argtest" ] && continue   # handled specially with args
    [ "$name" = "pkgmain" ] && continue   # handled specially with SURU_MOD_PATH
    [[ "$name" == gcstress* ]] && continue  # handled specially with low GC threshold
    tests=$((tests + 1))
    outfile="$DIR/$name.out"
    exitfile="$DIR/$name.exit"
    want_code=0
    [ -f "$exitfile" ] && want_code="$(cat "$exitfile")"

    actual="$("$SURU" run "$src" 2>"$DIR/.stderr.$name")"
    got_code=$?

    if [ "$got_code" -ne "$want_code" ]; then
        echo "FAIL: $name (exit $got_code, want $want_code)"
        sed 's/^/    stderr: /' "$DIR/.stderr.$name"
        fail=$((fail + 1))
        continue
    fi
    if [ -f "$outfile" ] && [ "$actual" != "$(cat "$outfile")" ]; then
        echo "FAIL: $name (stdout mismatch)"
        echo "  got: $actual"
        echo "  want: $(cat "$outfile")"
        fail=$((fail + 1))
        continue
    fi
    echo "ok: $name"
done

# --syntax flag selection.
tests=$((tests + 3))
if "$SURU" run --syntax=legacy "$DIR/hello.suru" >/dev/null 2>&1; then
    echo "ok: syntax-legacy"
else
    echo "FAIL: syntax-legacy"
    fail=$((fail + 1))
fi
if "$SURU" --syntax=natural check "$ROOT/tests/natural/control_valid.suru" >/dev/null 2>&1; then
    echo "ok: syntax-natural-check"
else
    echo "FAIL: syntax-natural-check"
    fail=$((fail + 1))
fi
badnat="$(mktemp)"
printf 'routine f() -> Whole\n    give 1 + "x"\nend routine\n' > "$badnat"
if ! "$SURU" --syntax=natural check "$badnat" >/dev/null 2>&1; then
    echo "ok: syntax-natural-reject"
else
    echo "FAIL: syntax-natural-reject"
    fail=$((fail + 1))
fi
rm -f "$badnat"
"$SURU" --syntax=foo check "$DIR/hello.suru" >/dev/null 2>&1
code=$?
if [ "$code" -eq 2 ]; then
    echo "ok: syntax-invalid"
else
    echo "FAIL: syntax-invalid (exit $code)"
    fail=$((fail + 1))
fi

# Bytecode round-trip: compile hello.suru to .sbc and exec it.
tmp="$(mktemp -d)"
tests=$((tests + 1))
if "$SURU" compile "$DIR/hello.suru" "$tmp/hello.sbc" >/dev/null 2>&1 &&
   [ -s "$tmp/hello.sbc" ]; then
    actual="$("$SURU" exec "$tmp/hello.sbc" 2>"$tmp/stderr")"
    code=$?
    if [ "$code" -eq 0 ] && [ "$actual" = "hello world" ]; then
        echo "ok: sbc-exec"
    else
        echo "FAIL: sbc-exec (exit $code)"
        sed 's/^/    stderr: /' "$tmp/stderr"
        fail=$((fail + 1))
    fi
else
    echo "FAIL: sbc-exec (compile failed)"
    fail=$((fail + 1))
fi

# Natural module writer: build a bytecode module from the selfhost writer and exec it.
tests=$((tests + 1))
if "$SURU" run "$ROOT/tests/selfhost/module_write_driver.suru" "$tmp/nat.sbc" >/dev/null 2>&1 &&
   [ -s "$tmp/nat.sbc" ]; then
    actual="$("$SURU" exec "$tmp/nat.sbc" 2>"$tmp/stderr")"
    if [ "$actual" = "ok" ]; then
        echo "ok: nat-module-write"
    else
        echo "FAIL: nat-module-write (output '$actual')"
        fail=$((fail + 1))
    fi
else
    echo "FAIL: nat-module-write (write failed)"
    fail=$((fail + 1))
fi

# Natural run: execute a minimal Natural program end-to-end.
tests=$((tests + 1))
natprog="$(mktemp)"
printf 'routine main() -> Unit\n    value x: Whole = 1 + 2\n    show(x)\nend routine\n' > "$natprog"
actual="$("$SURU" --syntax=natural run "$natprog" 2>"$tmp/stderr")"
code=$?
if [ "$code" -eq 0 ] && [ "$actual" = "3" ]; then
    echo "ok: nat-run"
else
    echo "FAIL: nat-run (exit $code, output '$actual')"
    sed 's/^/    stderr: /' "$tmp/stderr"
    fail=$((fail + 1))
fi
rm -f "$natprog"

# Natural run: Text concatenation (literal + binding, and binding + binding).
tests=$((tests + 2))
natprog="$(mktemp)"
printf 'routine main() -> Unit\n    value n: Text = "world"\n    show("hello " + n)\nend routine\n' > "$natprog"
actual="$("$SURU" --syntax=natural run "$natprog" 2>"$tmp/stderr")"
code=$?
if [ "$code" -eq 0 ] && [ "$actual" = "hello world" ]; then
    echo "ok: nat-run-concat"
else
    echo "FAIL: nat-run-concat (exit $code, output '$actual')"
    sed 's/^/    stderr: /' "$tmp/stderr"
    fail=$((fail + 1))
fi
rm -f "$natprog"
natprog="$(mktemp)"
printf 'routine main() -> Unit\n    value a: Text = "foo"\n    value b: Text = "bar"\n    show(a + b)\nend routine\n' > "$natprog"
actual="$("$SURU" --syntax=natural run "$natprog" 2>"$tmp/stderr")"
code=$?
if [ "$code" -eq 0 ] && [ "$actual" = "foobar" ]; then
    echo "ok: nat-run-concat2"
else
    echo "FAIL: nat-run-concat2 (exit $code, output '$actual')"
    sed 's/^/    stderr: /' "$tmp/stderr"
    fail=$((fail + 1))
fi
rm -f "$natprog"

# Program arguments: run argtest.suru with two args.
tests=$((tests + 1))
actual="$("$SURU" run "$DIR/argtest.suru" alpha beta 2>"$tmp/stderr")"
code=$?
if [ "$code" -eq 0 ] && [ "$actual" = "$(cat "$DIR/argtest.out")" ]; then
    echo "ok: argtest"
else
    echo "FAIL: argtest (exit $code)"
    echo "  got: $actual"
    sed 's/^/    stderr: /' "$tmp/stderr"
    fail=$((fail + 1))
fi

# Module search path: SURU_MOD_PATH is used to resolve `use`.
tests=$((tests + 1))
mkdir -p "$tmp/mod"
printf 'func greet() -> str { return "gm"; }\n' > "$tmp/mod/gm.suru"
actual="$(SURU_MOD_PATH="$tmp/mod" "$SURU" run "$DIR/pkgmain.suru" 2>"$tmp/stderr")"
code=$?
if [ "$code" -eq 0 ] && [ "$actual" = "gm" ]; then
    echo "ok: pkgsearch"
else
    echo "FAIL: pkgsearch (exit $code)"
    echo "  got: $actual"
    sed 's/^/    stderr: /' "$tmp/stderr"
    fail=$((fail + 1))
fi

# GC stress: force frequent collections with a tiny threshold.
for src in "$DIR"/gcstress*.suru; do
    [ -e "$src" ] || continue
    name="$(basename "$src" .suru)"
    tests=$((tests + 1))
    actual="$(SURU_GC_THRESHOLD=512 "$SURU" run "$src" 2>"$tmp/stderr")"
    code=$?
    if [ "$code" -eq 0 ] && [ "$actual" = "$(cat "$DIR/$name.out")" ]; then
        echo "ok: $name"
    else
        echo "FAIL: $name (exit $code)"
        echo "  got: $actual"
        sed 's/^/    stderr: /' "$tmp/stderr"
        fail=$((fail + 1))
    fi
done

rm -rf "$tmp"

rm -f "$DIR"/.stderr.*
echo "integration: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
