#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

for input in control_valid.suru declarations_semantic_valid.suru modules_valid.suru \
             record_choice_valid.suru effects_valid.suru selfhost_tokenizer.suru; do
    if ! "$SURU" run "$DIR/natural_frontend_driver.suru" \
            "$ROOT/tests/natural/$input" >/dev/null 2>&1; then
        echo "natural frontend: $input failed the unified pipeline" >&2
        exit 1
    fi
done

# Array-builtin typing (array_concat / slice / range) must type-check.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
printf 'routine main() -> Unit\n    value a: [Whole] = [1]\n    value b: [Whole] = array_concat(a, [2])\n    change c: [Whole] = [1]\n    set c to slice(c, 0, 0)\n    value r: [Whole] = range(0, 3)\n    show(array_len(r))\nend routine\n' > "$tmp/arrays.suru"
if ! "$SURU" run "$DIR/natural_frontend_driver.suru" "$tmp/arrays.suru" >/dev/null 2>&1; then
    echo "natural frontend: array builtin typing failed" >&2
    exit 1
fi

echo "natural frontend: unified lex/parse/semantic/effect/bytecode pipeline passed"
