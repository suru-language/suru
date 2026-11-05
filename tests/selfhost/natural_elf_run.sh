#!/usr/bin/env bash
# Native ELF64 seed gate: the SURU-written ELF emitter must produce a module
# x86-64 ELF64 executable (no C compiler/linker) that runs and prints "hi".
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_elf_mk_driver.suru" "$tmp/minimal.elf"

if ! file "$tmp/minimal.elf" | grep -q "ELF 64-bit LSB executable, x86-64"; then
    echo "FAIL: emitted file is not an x86-64 ELF64 executable" >&2
    exit 1
fi

chmod +x "$tmp/minimal.elf"
out="$("$tmp/minimal.elf")"
rc=$?
if [ "$rc" -ne 0 ] || [ "$out" != "hi" ]; then
    echo "FAIL: minimal ELF ran with rc=$rc out='$out'" >&2
    exit 1
fi

echo "natural ELF emitter: 1 tests, 0 failures"
