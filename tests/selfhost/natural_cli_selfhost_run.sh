#!/usr/bin/env bash
# Exercise the public CLI route to the SURU-written Stage 1 compiler.
set -eu

SURU="${1:-build/bin/suru}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

source="$tmp/source with spaces.suru"
reference="$tmp/reference output.sbc"
selfhost="$tmp/selfhost output.sbc"
cp "$ROOT/tests/natural/per_function_slots.suru" "$source"

"$SURU" --syntax=natural compile "$source" "$reference"
"$SURU" --syntax=natural --compiler=selfhost compile "$source" "$selfhost"
cmp "$reference" "$selfhost"

"$SURU" --syntax=natural --compiler=selfhost check "$source"
actual="$("$SURU" --syntax=natural --compiler=selfhost run "$source")"
if [ "$actual" != "304" ]; then
    echo "FAIL: self-hosted CLI run returned '$actual', expected '304'" >&2
    exit 1
fi

execout="$("$SURU" exec "$selfhost")"
if [ "$execout" != "304" ]; then
    echo "FAIL: self-hosted CLI module returned '$execout', expected '304'" >&2
    exit 1
fi

if "$SURU" --compiler=selfhost check "$ROOT/tests/integration/hello.suru" >/dev/null 2>&1; then
    echo "FAIL: self-hosted compiler accepted legacy syntax mode" >&2
    exit 1
fi
if "$SURU" --syntax=natural --compiler=selfhost --backend=native compile \
       "$source" "$tmp/native.elf" >/dev/null 2>&1; then
    echo "FAIL: self-hosted bytecode compiler accepted unsupported native backend" >&2
    exit 1
fi

echo "self-hosted compiler CLI: compile/check/run parity passed"
