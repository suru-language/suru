#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$SURU" run "$DIR/natural_vm_closure_string_mk_driver.suru" "$tmp/clo-string.sbc" 2>/dev/null
if ! c="$("$SURU" exec "$tmp/clo-string.sbc" 2>&1)"; then
    echo "FAIL managed closure capture: C VM failed: [$c]" >&2
    exit 1
fi
if ! s="$(SURU_GC_ALLOC_INTERVAL=1000000 "$SURU" run "$DIR/natural_vm_driver.suru" "$tmp/clo-string.sbc" 2>&1)"; then
    echo "FAIL managed closure capture: SURU VM failed: [$s]" >&2
    exit 1
fi
if [ "$c" != "captured" ] || [ "$s" != "$c" ]; then
    echo "FAIL managed closure capture: C=[$c] SURU=[$s]" >&2
    exit 1
fi
echo "natural VM managed closures: captured heap string survived closure creation and call"
