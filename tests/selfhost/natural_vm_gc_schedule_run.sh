#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
"$SURU" run "$DIR/natural_vm_gc_schedule_driver.suru"
