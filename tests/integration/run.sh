#!/usr/bin/env bash
# Integration test runner: for each <name>.suru, run `suru run` and compare
# stdout to <name>.out and exit code to <name>.exit (default 0).
set -u

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
fail=0
tests=0

for src in "$DIR"/*.suru; do
    [ -e "$src" ] || continue
    name="$(basename "$src" .suru)"
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

rm -rf "$tmp"

rm -f "$DIR"/.stderr.*
echo "integration: $tests tests, $fail failures"
[ "$fail" -eq 0 ]
