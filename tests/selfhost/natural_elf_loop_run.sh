#!/usr/bin/env bash
# Native control-flow gate: the SURU-written x86-64 ELF emitter must produce an
# executable for `change i=0; repeat while i<3 { show(i); i=i+1 }` that prints
# "012".
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_elf_repeat_mk_driver.suru" "$tmp/loop.elf"

if ! file "$tmp/loop.elf" | grep -q "ELF 64-bit LSB executable, x86-64"; then
    echo "FAIL: loop output is not an x86-64 ELF64 executable" >&2
    exit 1
fi

chmod +x "$tmp/loop.elf"
out="$("$tmp/loop.elf")"
rc=$?
if [ "$rc" -ne 0 ] || [ "$out" != "012" ]; then
    echo "FAIL: native loop ran rc=$rc out='$out'" >&2
    exit 1
fi

echo "natural ELF loop: 1 tests, 0 failures"
