#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$SURU" run "$DIR/natural_vm_exception_mk_driver.suru" "$tmp/exception.sbc" 2>/dev/null
c="$("$SURU" exec "$tmp/exception.sbc" 2>&1)"
s="$("$SURU" run "$DIR/natural_vm_driver.suru" "$tmp/exception.sbc" 2>&1)"
if [ "$c" != "caught" ] || [ "$s" != "$c" ]; then
    echo "FAIL managed exception: C=[$c] SURU=[$s]" >&2
    exit 1
fi
echo "natural VM managed exceptions: cause chain and typed catch parity passed"
