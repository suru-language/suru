#!/usr/bin/env bash
# Stage-4 seed execution gate: the Natural-syntax tokenizer must run through the
# default C pipeline and emit the expected token stream (one per line).
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

out="$("$SURU" --syntax=natural run "$ROOT/tests/natural/selfhost_tokenizer.suru" 2>&1)"
want="show,(,value,+,2,),"

if [ "$out" != "$want" ]; then
    echo "FAIL: tokenizer output '$out' != expected" >&2
    exit 1
fi

echo "natural tokenizer: 1 tests, 0 failures"
