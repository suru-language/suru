#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
REFERENCE="${2:-build/unit/selfhost_reference_lexer}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/lexer_driver.suru" >"$tmp/suru.out"
"$REFERENCE" "$DIR/lexer_input.suru" >"$tmp/reference.out"
diff -u "$tmp/reference.out" "$tmp/suru.out"
echo "selfhost lexer: token stream matches reference"

if "$SURU" run "$DIR/lexer_negative_driver.suru" >"$tmp/suru-invalid.out"; then
    echo "selfhost lexer: invalid input was accepted" >&2
    exit 1
fi
if "$REFERENCE" "$DIR/lexer_invalid.suru" >"$tmp/reference-invalid.out"; then
    echo "reference lexer: invalid input was accepted" >&2
    exit 1
fi
echo "selfhost lexer: invalid input rejected by both implementations"
