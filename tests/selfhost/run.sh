#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
REFERENCE="${2:-build/unit/selfhost_reference_lexer}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for input in lexer_input.suru lexer_literals.suru; do
    "$SURU" run "$DIR/lexer_driver.suru" "$DIR/$input" >"$tmp/suru.out"
    "$REFERENCE" "$DIR/$input" >"$tmp/reference.out"
    diff -u "$tmp/reference.out" "$tmp/suru.out"
done
echo "selfhost lexer: token kinds, locations, and literal values match reference"

for input in lexer_invalid.suru lexer_badhex.suru lexer_badescape.suru; do
    if "$SURU" run "$DIR/lexer_negative_driver.suru" "$DIR/$input" >"$tmp/suru-invalid.out"; then
        echo "selfhost lexer: $input was accepted" >&2
        exit 1
    fi
    if "$REFERENCE" "$DIR/$input" >"$tmp/reference-invalid.out"; then
        echo "reference lexer: $input was accepted" >&2
        exit 1
    fi
    diff -u "$tmp/reference-invalid.out" "$tmp/suru-invalid.out"
done
echo "selfhost lexer: invalid diagnostics match reference"
