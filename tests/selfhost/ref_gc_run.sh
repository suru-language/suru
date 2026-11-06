#!/usr/bin/env bash
# SURU GC reference model gate: runs the pure-SURU tri-color mark-sweep
# model (selfhost/memory/ref_gc.suru) and requires all reachability,
# cycle, root, overflow, allocation-limit, and D7 edge scenarios to pass.
set -eu

SURU="${1:-build/bin/suru}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

OUT="$("$SURU" run "$ROOT/selfhost/memory/ref_gc.suru")"

if ! printf '%s\n' "$OUT" | grep -q "REF-GC: all tests passed"; then
    echo "natural GC reference model: FAILED" >&2
    exit 1
fi

CHECKS="$(printf '%s\n' "$OUT" | grep -c '^ok:')"
echo "natural GC reference model: $CHECKS checks passed"
