#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
if ! "$SURU" run "$DIR/natural_vm_local_roots_mk_driver.suru" "$tmp/local-roots.sbc" 2>"$tmp/driver.err"; then
    echo "FAIL suspended caller roots: module driver failed" >&2
    cat "$tmp/driver.err" >&2
    exit 1
fi
if ! c="$("$SURU" exec "$tmp/local-roots.sbc" 2>&1)"; then
    echo "FAIL suspended caller roots: C VM failed: [$c]" >&2
    exit 1
fi
if ! s="$("$SURU" run "$DIR/natural_vm_driver.suru" "$tmp/local-roots.sbc" 2>&1)"; then
    echo "FAIL suspended caller roots: SURU VM failed: [$s]" >&2
    exit 1
fi
if [ "$c" != "caller-root" ] || [ "$s" != "$c" ]; then
    echo "FAIL suspended caller roots: C=[$c] SURU=[$s]" >&2
    exit 1
fi
echo "natural VM caller-frame roots: managed local survived 520 callee allocations"
