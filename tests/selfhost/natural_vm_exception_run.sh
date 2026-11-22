#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
if ! "$SURU" run "$DIR/natural_vm_exception_mk_driver.suru" "$tmp/exception.sbc" 2>"$tmp/driver.err"; then
    echo "FAIL managed exception: module driver failed" >&2
    cat "$tmp/driver.err" >&2
    exit 1
fi
if ! c="$("$SURU" exec "$tmp/exception.sbc" 2>&1)"; then
    echo "FAIL managed exception: C VM failed: [$c]" >&2
    exit 1
fi
if ! s="$(SURU_GC_ALLOC_INTERVAL=1000000 "$SURU" run "$DIR/natural_vm_driver.suru" "$tmp/exception.sbc" 2>&1)"; then
    echo "FAIL managed exception: SURU VM failed: [$s]" >&2
    exit 1
fi
if [ "$c" != "caught" ] || [ "$s" != "$c" ]; then
    echo "FAIL managed exception: C=[$c] SURU=[$s]" >&2
    exit 1
fi
echo "natural VM managed exceptions: cause chain and typed catch parity passed"
