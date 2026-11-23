#!/usr/bin/env bash
set -eu
SURU="${1:-build/bin/suru}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
echo "AST bytecode regression: baseline routine/control fixture"
source="$ROOT/tests/natural/routine_ast_bytecode.suru"
"$SURU" --syntax=natural compile "$source" "$tmp/reference.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$source" "$tmp/ast.sbc"
if ! cmp -s "$tmp/reference.sbc" "$tmp/ast.sbc"; then
    echo "AST bytecode differs from reference:" >&2
    cmp -l "$tmp/reference.sbc" "$tmp/ast.sbc" | head -12 >&2 || true
    echo "reference:" >&2
    od -An -tx1 -N128 "$tmp/reference.sbc" >&2
    echo "AST:" >&2
    od -An -tx1 -N128 "$tmp/ast.sbc" >&2
    exit 1
fi
actual="$("$SURU" exec "$tmp/ast.sbc")"
expected="$(printf '5\n5')"
if [ "$actual" != "$expected" ]; then
    echo "AST bytecode execution mismatch: '$actual'" >&2
    exit 1
fi

echo "AST bytecode regression: simple explicit choice match fixture"
choice_source="$DIR/natural_ast_choose_simple.suru"
"$SURU" --syntax=natural compile "$choice_source" "$tmp/reference-choose.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$choice_source" "$tmp/ast-choose.sbc"
if ! cmp -s "$tmp/reference-choose.sbc" "$tmp/ast-choose.sbc"; then
    echo "AST choice-match bytecode differs from reference" >&2
    cmp -l "$tmp/reference-choose.sbc" "$tmp/ast-choose.sbc" | head -12 >&2 || true
    exit 1
fi
choice_actual="$("$SURU" exec "$tmp/ast-choose.sbc")"
choice_expected="up"
if [ "$choice_actual" != "$choice_expected" ]; then
    echo "AST choice-match C VM execution mismatch: '$choice_actual'" >&2
    exit 1
fi
choice_suru_actual="$(timeout -k 1s 10s "$SURU" run \
    "$DIR/natural_vm_driver.suru" "$tmp/ast-choose.sbc" 2>&1)"
if [ "$choice_suru_actual" != "$choice_expected" ]; then
    echo "AST choice-match SURU VM execution mismatch: '$choice_suru_actual'" >&2
    exit 1
fi

echo "AST bytecode regression: wildcard choice match fixture"
wildcard_source="$DIR/natural_ast_choose_wildcard.suru"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$wildcard_source" "$tmp/ast-choose-wildcard.sbc"
wildcard_actual="$("$SURU" exec "$tmp/ast-choose-wildcard.sbc")"
wildcard_expected="down"
if [ "$wildcard_actual" != "$wildcard_expected" ]; then
    echo "AST wildcard choice C VM mismatch: '$wildcard_actual'" >&2
    exit 1
fi
wildcard_suru_actual="$(timeout -k 1s 10s "$SURU" run \
    "$DIR/natural_vm_driver.suru" "$tmp/ast-choose-wildcard.sbc" 2>&1)"
if [ "$wildcard_suru_actual" != "$wildcard_expected" ]; then
    echo "AST wildcard choice SURU VM mismatch: '$wildcard_suru_actual'" >&2
    exit 1
fi
echo "natural AST bytecode: explicit and wildcard choice matching passed in both VMs"

echo "AST bytecode regression: choice payload binding fixture"
payload_source="$DIR/natural_ast_choose_payload.suru"
"$SURU" --syntax=natural compile "$payload_source" "$tmp/reference-choose-payload.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$payload_source" "$tmp/ast-choose-payload.sbc"
if ! cmp -s "$tmp/reference-choose-payload.sbc" "$tmp/ast-choose-payload.sbc"; then
    echo "AST choice-payload bytecode differs from reference" >&2
    cmp -l "$tmp/reference-choose-payload.sbc" "$tmp/ast-choose-payload.sbc" | head -12 >&2 || true
    exit 1
fi
payload_actual="$("$SURU" exec "$tmp/ast-choose-payload.sbc")"
payload_expected="$(printf 'node\noffline')"
if [ "$payload_actual" != "$payload_expected" ]; then
    echo "AST choice payload C VM mismatch: '$payload_actual'" >&2
    exit 1
fi
payload_suru_actual="$(timeout -k 1s 10s "$SURU" run \
    "$DIR/natural_vm_driver.suru" "$tmp/ast-choose-payload.sbc" 2>&1)"
if [ "$payload_suru_actual" != "$payload_expected" ]; then
    echo "AST choice payload SURU VM mismatch: '$payload_suru_actual'" >&2
    exit 1
fi
echo "natural AST bytecode: typed payload patterns matched reference bytecode and both VMs"

echo "AST bytecode regression: nested forward routine-call fixture"
call_source="$DIR/natural_ast_call_types.suru"
if ! timeout -k 1s 10s "$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
       "$call_source" "$tmp/ast-calls.sbc" >"$tmp/calls-compile.log" 2>&1; then
    echo "AST nested forward-call compilation failed:" >&2
    cat "$tmp/calls-compile.log" >&2
    exit 1
fi
echo "AST nested-call compiler driver completed successfully"
echo "AST nested forward-call fixture: executing in C VM"
if ! calls_actual="$(timeout -k 1s 10s "$SURU" exec "$tmp/ast-calls.sbc" 2>&1)"; then
    echo "AST nested routine-call C VM failed:" >&2
    printf '%s\n' "$calls_actual" >&2
    exit 1
fi
calls_expected="6"
if [ "$calls_actual" != "$calls_expected" ]; then
    echo "AST nested routine-call C VM mismatch: '$calls_actual'" >&2
    exit 1
fi
echo "AST nested forward-call fixture: executing in SURU VM"
if ! calls_suru_actual="$(timeout -k 1s 10s "$SURU" run \
    "$DIR/natural_vm_driver.suru" "$tmp/ast-calls.sbc" 2>&1)"; then
    echo "AST nested routine-call SURU VM failed:" >&2
    printf '%s\n' "$calls_suru_actual" >&2
    exit 1
fi
if [ "$calls_suru_actual" != "$calls_expected" ]; then
    echo "AST nested routine-call SURU VM mismatch: '$calls_suru_actual'" >&2
    exit 1
fi
echo "AST nested forward-call fixture: both VMs passed"

bad_type_source="$DIR/natural_ast_bad_call_type.suru"
if "$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
       "$bad_type_source" "$tmp/bad-call-type.sbc" >"$tmp/bad-call-type.log" 2>&1; then
    echo "AST bytecode accepted an invalid user-routine argument type" >&2
    exit 1
fi
if ! grep -q "argument 1 to 'identity' has type Text, expected Whole" "$tmp/bad-call-type.log"; then
    echo "Unexpected wrong-argument-type diagnostic:" >&2
    cat "$tmp/bad-call-type.log" >&2
    exit 1
fi

bad_arity_source="$DIR/natural_ast_bad_call_arity.suru"
if "$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
       "$bad_arity_source" "$tmp/bad-call-arity.sbc" >"$tmp/bad-call-arity.log" 2>&1; then
    echo "AST bytecode accepted an invalid user-routine argument count" >&2
    exit 1
fi
if ! grep -q "routine 'identity' expects 1 arguments, got 0" "$tmp/bad-call-arity.log"; then
    echo "Unexpected wrong-argument-count diagnostic:" >&2
    cat "$tmp/bad-call-arity.log" >&2
    exit 1
fi
echo "natural AST bytecode: nested forward calls, return-type inference, and invalid call rejection passed"
echo "AST bytecode regression: range-loop fixture"
range_source="$ROOT/tests/natural/routine_ast_each_range.suru"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$range_source" "$tmp/ast-each-range.sbc"
range_actual="$("$SURU" exec "$tmp/ast-each-range.sbc")"
range_expected="$(printf '6\n1\n2\n3\n9223372036854775807')"
if [ "$range_actual" != "$range_expected" ]; then
    echo "AST each-range execution mismatch: '$range_actual'" >&2
    exit 1
fi
echo "AST bytecode regression: indexed-assignment fixture"
indexed_source="$ROOT/tests/natural/routine_ast_index_assignment.suru"
"$SURU" --syntax=natural compile "$indexed_source" "$tmp/reference-index.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$indexed_source" "$tmp/ast-index.sbc"
if ! cmp -s "$tmp/reference-index.sbc" "$tmp/ast-index.sbc"; then
    echo "AST indexed-assignment bytecode differs from reference" >&2
    cmp -l "$tmp/reference-index.sbc" "$tmp/ast-index.sbc" | head -12 >&2 || true
    exit 1
fi
indexed_actual="$("$SURU" exec "$tmp/ast-index.sbc")"
indexed_expected="$(printf '2\n9')"
if [ "$indexed_actual" != "$indexed_expected" ]; then
    echo "AST indexed-assignment execution mismatch: '$indexed_actual'" >&2
    exit 1
fi
echo "AST bytecode regression: record-field assignment fixture"
record_source="$ROOT/tests/natural/routine_ast_record_field_assignment.suru"
"$SURU" --syntax=natural compile "$record_source" "$tmp/reference-record.sbc"
"$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$record_source" "$tmp/ast-record.sbc"
if ! cmp -s "$tmp/reference-record.sbc" "$tmp/ast-record.sbc"; then
    echo "AST record-field bytecode differs from reference" >&2
    cmp -l "$tmp/reference-record.sbc" "$tmp/ast-record.sbc" | head -12 >&2 || true
    exit 1
fi
record_actual="$("$SURU" exec "$tmp/ast-record.sbc")"
record_expected="41"
if [ "$record_actual" != "$record_expected" ]; then
    echo "AST record-field C VM execution mismatch: '$record_actual'" >&2
    exit 1
fi
suru_vm_record_actual="$(timeout -k 1s 10s "$SURU" run \
    "$DIR/natural_vm_driver.suru" "$tmp/ast-record.sbc" 2>&1)"
if [ "$suru_vm_record_actual" != "$record_expected" ]; then
    echo "AST record-field SURU VM execution mismatch: '$suru_vm_record_actual'" >&2
    exit 1
fi
echo "natural AST bytecode: record-field assignment matched reference bytecode and passed both VMs"
echo "AST bytecode regression: array-across fixture"
across_source="$DIR/natural_ast_each_across.suru"
echo "natural AST bytecode: compiling each-across fixture"
timeout -k 1s 5s "$SURU" run "$DIR/natural_ast_bytecode_driver.suru" \
    "$across_source" "$tmp/ast-across.sbc"
echo "natural AST bytecode: executing each-across module"
across_actual="$(timeout -k 1s 5s "$SURU" exec "$tmp/ast-across.sbc")"
across_expected="$(printf '4\n5\n6\n15')"
if [ "$across_actual" != "$across_expected" ]; then
    echo "AST each-across C VM execution mismatch: '$across_actual'" >&2
    exit 1
fi
suru_vm_across_actual="$(timeout -k 1s 10s "$SURU" run \
    "$DIR/natural_vm_driver.suru" "$tmp/ast-across.sbc" 2>&1)"
if [ "$suru_vm_across_actual" != "$across_expected" ]; then
    echo "AST each-across SURU VM execution mismatch: '$suru_vm_across_actual'" >&2
    exit 1
fi
echo "natural AST bytecode: each-across array iteration passed C VM and SURU VM execution"
echo "natural AST bytecode: exclusive/inclusive ranges, indexed writes, predeclared calls, nested control flow, and routine output passed"
