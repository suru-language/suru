#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_bytecode_driver.suru" \
    "$ROOT/tests/natural/bytecode_effects.suru" >"$tmp/bytecode.out"
grep -q '^bc|0|BC_PUSH_HANDLER|4|\*$' "$tmp/bytecode.out"
grep -q '^bc|2|BC_POP_HANDLER|$' "$tmp/bytecode.out"
grep -q '^bc|3|BC_JMP|8$' "$tmp/bytecode.out"
grep -q '^bc|4|BC_NOP|handler:FileError$' "$tmp/bytecode.out"
grep -q '^bc|8|BC_NOP|cleanup$' "$tmp/bytecode.out"
grep -q '^bc|9|BC_NOP|show("cleanup")$' "$tmp/bytecode.out"
grep -q 'BC_JMP|13$' "$tmp/bytecode.out"
grep -q '^bc|13|BC_RET_UNIT|$' "$tmp/bytecode.out"
if grep -q '^bc|.*|LABEL|' "$tmp/bytecode.out"; then
    echo "natural bytecode lowering: unresolved label emitted" >&2
    exit 1
fi

echo "natural bytecode lowering: effect handler corpus passed"
