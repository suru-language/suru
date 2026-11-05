#!/usr/bin/env bash
# Closure gate: a hand-built module using BC_MKCLOSURE_V / BC_CALLV must
# produce identical output ("5") on the C17 VM and the SURU VM.
set -eu

SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$SURU" run "$DIR/natural_closure_mk_driver.suru" "$tmp/clo.sbc" 2>/dev/null
c=$("$SURU" exec "$tmp/clo.sbc" 2>&1)
s=$("$SURU" run "$DIR/natural_vm_driver.suru" "$tmp/clo.sbc" 2>&1)

if [ "$c" = "5" ] && [ "$s" = "5" ]; then
    echo "natural VM closures: 1 tests, 0 failures"
else
    echo "FAIL: closures C=[$c] SURU=[$s]" >&2
    exit 1
fi
