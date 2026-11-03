#!/usr/bin/env bash
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/effect_runtime_driver.suru" >"$tmp/effects.out"
grep -q '^normal|normal$' "$tmp/effects.out"
grep -q '^return|return|42$' "$tmp/effects.out"
grep -q '^failure|exception|FileError|read failed$' "$tmp/effects.out"
grep -q '^replaced|exception|CleanupError|close failed|cause=exception|FileError|read failed$' "$tmp/effects.out"
grep -q '^handled|normal$' "$tmp/effects.out"
grep -q '^unhandled|exception|FileError|read failed$' "$tmp/effects.out"
grep -q '^nested|exception|CleanupError|close failed|cause=exception|FileError|read failed$' "$tmp/effects.out"

echo "natural effect runtime: exception and cleanup semantics passed"
