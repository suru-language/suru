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
