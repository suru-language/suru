#include "suru/stdlib.h"

#include <string.h>

const s_builtin_sig s_builtin_table[S_BUILTIN_COUNT] = {
    [S_BUILTIN_PRINT] = {
        "print", 1, { S_SIG_ANY }, S_SIG_UNIT },
    [S_BUILTIN_PRINTLN] = {
        "println", 1, { S_SIG_ANY }, S_SIG_UNIT },
    [S_BUILTIN_STR_LEN] = {
        "str_len", 1, { S_SIG_STR }, S_SIG_INT },
    [S_BUILTIN_ARRAY_LEN] = {
        "array_len", 1, { S_SIG_ARRAY }, S_SIG_INT },
    [S_BUILTIN_READ_LINE] = {
        "read_line", 0, { S_SIG_UNIT }, S_SIG_STR },
    [S_BUILTIN_TO_STR] = {
        "to_str", 1, { S_SIG_ANY }, S_SIG_STR },
    [S_BUILTIN_STR_COMPARE] = {
        "str_compare", 2, { S_SIG_STR, S_SIG_STR }, S_SIG_INT },
    [S_BUILTIN_TYPE_OF] = {
        "type_of", 1, { S_SIG_ANY }, S_SIG_STR },
    [S_BUILTIN_F64_TO_STR] = {
        "f64_to_str", 1, { S_SIG_FLOAT }, S_SIG_STR },
    [S_BUILTIN_I64_TO_F64] = {
        "i64_to_f64", 1, { S_SIG_INT }, S_SIG_FLOAT },
    [S_BUILTIN_F64_TO_I64] = {
        "f64_to_i64", 1, { S_SIG_FLOAT }, S_SIG_INT },
    [S_BUILTIN_MAKE_ARRAY] = {
        "make_array", 1, { S_SIG_INT }, S_SIG_ARRAY_INT },
    [S_BUILTIN_RANGE] = {
        "range", 2, { S_SIG_INT, S_SIG_INT }, S_SIG_ARRAY_INT },
    [S_BUILTIN_SUBSTR] = {
        "substr", 3, { S_SIG_STR, S_SIG_INT, S_SIG_INT }, S_SIG_STR },
    [S_BUILTIN_STR_CONTAINS] = {
        "str_contains", 2, { S_SIG_STR, S_SIG_STR }, S_SIG_BOOL },
    [S_BUILTIN_INT_FROM_STR] = {
        "int_from_str", 1, { S_SIG_STR }, S_SIG_INT },
    [S_BUILTIN_ABS] = {
        "abs", 1, { S_SIG_INT }, S_SIG_INT },
    [S_BUILTIN_MIN] = {
        "min", 2, { S_SIG_INT, S_SIG_INT }, S_SIG_INT },
    [S_BUILTIN_MAX] = {
        "max", 2, { S_SIG_INT, S_SIG_INT }, S_SIG_INT },
    [S_BUILTIN_STR_TO_F64] = {
        "str_to_f64", 1, { S_SIG_STR }, S_SIG_FLOAT },
    [S_BUILTIN_RANGE_STEP] = {
        "range_step", 3, { S_SIG_INT, S_SIG_INT, S_SIG_INT }, S_SIG_ARRAY_INT },
    [S_BUILTIN_ASSERT] = {
        "assert", 2, { S_SIG_BOOL, S_SIG_STR }, S_SIG_UNIT },
    [S_BUILTIN_EXIT] = {
        "exit", 1, { S_SIG_INT }, S_SIG_UNIT },
    [S_BUILTIN_TO_HEX] = {
        "to_hex", 1, { S_SIG_INT }, S_SIG_STR },
    [S_BUILTIN_TO_BIN] = {
        "to_bin", 1, { S_SIG_INT }, S_SIG_STR },
    [S_BUILTIN_F64_TO_STR_PREC] = {
        "f64_to_str_prec", 2, { S_SIG_FLOAT, S_SIG_INT }, S_SIG_STR },
    [S_BUILTIN_STR_REPEAT] = {
        "str_repeat", 2, { S_SIG_STR, S_SIG_INT }, S_SIG_STR },
    [S_BUILTIN_STR_TRIM] = {
        "str_trim", 1, { S_SIG_STR }, S_SIG_STR },
    [S_BUILTIN_ARGS_LEN] = {
        "args_len", 0, { S_SIG_UNIT }, S_SIG_INT },
    [S_BUILTIN_ARGS] = {
        "args", 1, { S_SIG_INT }, S_SIG_STR },
    [S_BUILTIN_STR_SPLIT] = {
        "str_split", 2, { S_SIG_STR, S_SIG_STR }, S_SIG_ARRAY_STR },
    [S_BUILTIN_STR_JOIN] = {
        "str_join", 2, { S_SIG_ARRAY_STR, S_SIG_STR }, S_SIG_STR },
    [S_BUILTIN_ARRAY_SLICE] = {
        "slice", 3, { S_SIG_ARRAY, S_SIG_INT, S_SIG_INT }, S_SIG_ARRAY },
    [S_BUILTIN_STR_REVERSE] = {
        "str_reverse", 1, { S_SIG_STR }, S_SIG_STR },
    [S_BUILTIN_STR_UPPER] = {
        "str_upper", 1, { S_SIG_STR }, S_SIG_STR },
    [S_BUILTIN_STR_LOWER] = {
        "str_lower", 1, { S_SIG_STR }, S_SIG_STR },
    [S_BUILTIN_LEN] = {
        "len", 1, { S_SIG_ANY }, S_SIG_INT },
    [S_BUILTIN_ARRAY_REVERSE] = {
        "array_reverse", 1, { S_SIG_ARRAY }, S_SIG_ARRAY },
    [S_BUILTIN_ARRAY_SUM] = {
        "array_sum", 1, { S_SIG_ARRAY_INT }, S_SIG_INT },
    [S_BUILTIN_ARRAY_MIN] = {
        "array_min", 1, { S_SIG_ARRAY_INT }, S_SIG_INT },
    [S_BUILTIN_ARRAY_MAX] = {
        "array_max", 1, { S_SIG_ARRAY_INT }, S_SIG_INT },
    [S_BUILTIN_READ_FILE] = {
        "read_file", 1, { S_SIG_STR }, S_SIG_STR },
    [S_BUILTIN_WRITE_FILE] = {
        "write_file", 2, { S_SIG_STR, S_SIG_STR }, S_SIG_UNIT },
    [S_BUILTIN_FILE_EXISTS] = {
        "file_exists", 1, { S_SIG_STR }, S_SIG_BOOL },
};

const s_builtin_sig *s_builtin_lookup(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < S_BUILTIN_COUNT; ++i) {
        if (strcmp(s_builtin_table[i].name, name) == 0) {
            return &s_builtin_table[i];
        }
    }
    return NULL;
}
