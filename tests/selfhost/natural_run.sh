#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for input in lexical_valid.suru control_valid.suru \
             record_choice_valid.suru modules_valid.suru keywords_valid.suru; do
    "$SURU" run "$DIR/natural_lexer_driver.suru" \
        "$ROOT/tests/natural/$input" >"$tmp/$input.tokens"
    grep -q '^EOF|' "$tmp/$input.tokens"
done

diff -u "$ROOT/tests/natural/lexical_valid.tokens" \
        "$tmp/lexical_valid.suru.tokens"
grep -q '^KEYWORD|.*|value$' "$tmp/lexical_valid.suru.tokens"
grep -q '^STRING|.*|.*{name}.*$' "$tmp/lexical_valid.suru.tokens"
grep -q '^NEWLINE|' "$tmp/lexical_valid.suru.tokens"
for keyword in across as attempt bring case change choose constant contract each \
              end ensure exception expose false from give handle if in through \
              until otherwise record repeat routine set show supports true value \
              when while throws this none; do
    grep -q "^KEYWORD|.*|$keyword$" "$tmp/keywords_valid.suru.tokens" || {
        echo "natural lexer: missing keyword $keyword" >&2
        exit 1
    }
done

if "$SURU" run "$DIR/natural_lexer_driver.suru" \
       "$ROOT/tests/natural/invalid_interpolation.suru" \
       >"$tmp/invalid.out"; then
    echo "natural lexer: malformed interpolation was accepted" >&2
    exit 1
fi
grep -q '^error|204|' "$tmp/invalid.out"

if "$SURU" run "$DIR/natural_lexer_driver.suru" \
       "$ROOT/tests/natural/invalid_escape.suru" \
       >"$tmp/invalid-escape.out"; then
    echo "natural lexer: invalid escape was accepted" >&2
    exit 1
fi
grep -q '^error|203|.*|invalid-escape$' "$tmp/invalid-escape.out"

if "$SURU" run "$DIR/natural_lexer_driver.suru" \
       "$ROOT/tests/natural/lexical_invalid_utf8.suru" \
       >"$tmp/invalid-utf8.out"; then
    echo "natural lexer: malformed UTF-8 was accepted" >&2
    exit 1
fi
grep -q '^error|207|.*|invalid-utf8$' "$tmp/invalid-utf8.out"

echo "natural lexer: approved lexical corpus passed"
