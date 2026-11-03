#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_bytecode_driver.suru" \
    "$ROOT/tests/natural/bytecode_effects.suru" >"$tmp/bytecode.out"
grep -q '^bc|PUSH_HANDLER|handler0|\*$' "$tmp/bytecode.out"
grep -q '^bc|JMP|ensure0$' "$tmp/bytecode.out"
grep -q '^bc|LABEL|handler0|FileError$' "$tmp/bytecode.out"
grep -q '^bc|LABEL|ensure0$' "$tmp/bytecode.out"
grep -q '^bc|RET_UNIT|$' "$tmp/bytecode.out"
grep -q '^bc|LABEL|end-attempt0$' "$tmp/bytecode.out"
grep -q '^bc|END_FUNC|$' "$tmp/bytecode.out"

echo "natural bytecode lowering: effect handler corpus passed"
