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
        "to_str", 1, { S_SIG_INT }, S_SIG_STR },
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
