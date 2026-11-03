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
    "$SURU" run "$DIR/token_stream_driver.suru" "$DIR/$input" >"$tmp/stream.out"
    diff -u "$tmp/reference.out" "$tmp/stream.out"
done
echo "selfhost lexer/token stream: token kinds, locations, and literal values match reference"

for input in lexer_invalid.suru lexer_badhex.suru lexer_badbinary.suru \
             lexer_badescape.suru lexer_unterminated.suru lexer_unexpected.suru; do
    if "$SURU" run "$DIR/lexer_negative_driver.suru" "$DIR/$input" >"$tmp/suru-invalid.out"; then
        echo "selfhost lexer: $input was accepted" >&2
        exit 1
    fi
    if "$REFERENCE" "$DIR/$input" >"$tmp/reference-invalid.out"; then
        echo "reference lexer: $input was accepted" >&2
        exit 1
    fi
    diff -u "$tmp/reference-invalid.out" "$tmp/suru-invalid.out"
    if "$SURU" run "$DIR/token_stream_driver.suru" "$DIR/$input" >"$tmp/stream-invalid.out"; then
        echo "selfhost token stream: $input was accepted" >&2
        exit 1
    fi
    diff -u "$tmp/reference-invalid.out" "$tmp/stream-invalid.out"
done
echo "selfhost lexer/token stream: invalid diagnostics match reference"
