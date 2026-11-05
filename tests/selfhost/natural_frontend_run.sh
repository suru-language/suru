#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

for input in control_valid.suru declarations_semantic_valid.suru modules_valid.suru \
             record_choice_valid.suru effects_valid.suru; do
    if ! "$SURU" run "$DIR/natural_frontend_driver.suru" \
            "$ROOT/tests/natural/$input" >/dev/null 2>&1; then
        echo "natural frontend: $input failed the unified pipeline" >&2
        exit 1
    fi
done

echo "natural frontend: unified lex/parse/semantic/effect/bytecode pipeline passed"
