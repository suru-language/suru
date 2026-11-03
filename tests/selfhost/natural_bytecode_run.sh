#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_bytecode_driver.suru" \
    "$ROOT/tests/natural/bytecode_effects.suru" >"$tmp/bytecode.out"
grep -q '^pool|str|0|FileError$' "$tmp/bytecode.out"
grep -q '^pool|str|1|"try"$' "$tmp/bytecode.out"
grep -q '^pool|str|2|"handled"$' "$tmp/bytecode.out"
grep -q '^pool|str|3|"cleanup"$' "$tmp/bytecode.out"
grep -q '^bc|0|BC_PUSH_HANDLER|6|0$' "$tmp/bytecode.out"
grep -q '^bc|4|BC_POP_HANDLER|$' "$tmp/bytecode.out"
grep -q '^bc|5|BC_JMP|13$' "$tmp/bytecode.out"
grep -q '^bc|7|BC_NOP|handler:FileError$' "$tmp/bytecode.out"
grep -q '^bc|8|BC_CONST_STR|2$' "$tmp/bytecode.out"
grep -q '^bc|13|BC_NOP|cleanup$' "$tmp/bytecode.out"
grep -q '^bc|14|BC_CONST_STR|3$' "$tmp/bytecode.out"
grep -q 'BC_JMP|13$' "$tmp/bytecode.out"
grep -q '^bc|20|BC_RET_UNIT|$' "$tmp/bytecode.out"
if grep -q '^bc|.*|LABEL|' "$tmp/bytecode.out"; then
    echo "natural bytecode lowering: unresolved label emitted" >&2
    exit 1
fi

"$SURU" run "$DIR/natural_bytecode_driver.suru" \
    "$ROOT/tests/natural/bytecode_values.suru" >"$tmp/values.out"
grep -q '^pool|int|0|1$' "$tmp/values.out"
grep -q '^pool|int|1|2$' "$tmp/values.out"
grep -q '^pool|int|2|3$' "$tmp/values.out"
grep -q '^pool|int|3|4$' "$tmp/values.out"
grep -q 'BC_MUL_I' "$tmp/values.out"
grep -q 'BC_ADD_I' "$tmp/values.out"
grep -q 'BC_ST_LOCAL|0' "$tmp/values.out"
grep -q 'BC_LD_LOCAL|0' "$tmp/values.out"
if grep -q 'BC_NOP|value\|BC_NOP|set\|BC_NOP|show\|BC_NOP|give' "$tmp/values.out"; then
    echo "natural bytecode lowering: statement placeholder emitted" >&2
    exit 1
fi

echo "natural bytecode lowering: effect handler corpus passed"
