#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
# Keep the Stage 0 host collector out of this stress test; the SURU heap's
# interval/slice policy is exercised here, and the C host GC has separate gates.
SURU_GC_ALLOC_INTERVAL=1000000 SURU_GC_THRESHOLD=1099511627776 "$SURU" run "$DIR/heap_driver.suru"
