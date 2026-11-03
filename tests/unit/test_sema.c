#include "test_framework.h"
#include "suru/arena.h"
#include "suru/parser.h"
#include "suru/sema.h"
#include "suru/ast.h"

static bool compile_str(const char *src, s_module *out, s_arena *arena,
                        s_diag_ctx *d) {
    s_arena_init(arena);
    s_diag_init(d);
    s_parser p;
    bool ok = s_parse(&p, "t.suru", src, strlen(src), arena, d);
    if (!ok || s_diag_has_errors(d)) return false;
    *out = p.mod;
    return s_sema_run(out, arena, d);
}

static void test_valid_program(void) {
    const char *src2 =
        "func add(a: int, b: int) -> int { return a + b; }\n"
        "func main() -> int {\n"
        "    let x: int = add(1, 2);\n"
        "    return x;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src2, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    CHECK_EQ_INT(mod.main_index, 1);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_type_mismatch(void) {
    const char *src =
        "func main() -> int {\n"
        "    let x: int = true;\n"
        "    return x;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_unknown_var(void) {
    const char *src =
        "func main() -> int {\n"
        "    return z;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_missing_main(void) {
    const char *src = "func foo() -> int { return 1; }\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_immutable_assign(void) {
    const char *src =
        "func main() -> int {\n"
        "    let x = 1;\n"
        "    x = 2;\n"
        "    return x;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_arithmetic_type(void) {
    const char *src =
        "func main() -> int {\n"
        "    return 1 + 2 * 3;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_array_literal_valid(void) {
    const char *src =
        "func main() -> int {\n"
        "    let a = [1, 2, 3];\n"
        "    return a[1];\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_array_literal_hetero(void) {
    const char *src =
        "func main() -> int {\n"
        "    let a = [1, true];\n"
        "    return a[0];\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_array_literal_empty(void) {
    const char *src =
        "func main() -> int {\n"
        "    let a = [];\n"
        "    return 0;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_array_literal_empty_ascribed(void) {
    const char *src =
        "func main() -> int {\n"
        "    let a: [int] = [];\n"
        "    return array_len(a);\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_array_any_elem(void) {
    const char *src =
        "func main() -> int {\n"
        "    let a = [1.5, 2.5];\n"
        "    return array_len(a);\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_const_valid(void) {
    const char *src =
        "const X = 42;\n"
        "const NAME = \"suru\";\n"
        "func main() -> int {\n"
        "    return X;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_const_type_mismatch(void) {
    const char *src =
        "const X: int = 1.5;\n"
        "func main() -> int {\n"
        "    return 0;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_const_assign_rejected(void) {
    const char *src =
        "const X = 5;\n"
        "func main() -> int {\n"
        "    X = 9;\n"
        "    return 0;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_record_init_valid(void) {
    const char *src =
        "type Point { let x: int; let y: int; }\n"
        "func main() -> int {\n"
        "    let p = Point { y = 4, x = 3 };\n"
        "    return p.x + p.y;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_record_init_missing_field(void) {
    const char *src =
        "type Point { let x: int; let y: int; }\n"
        "func main() -> int {\n"
        "    let p = Point { x = 1 };\n"
        "    return p.x;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_record_init_unknown_field(void) {
    const char *src =
        "type Point { let x: int; }\n"
        "func main() -> int {\n"
        "    let p = Point { z = 1 };\n"
        "    return 0;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_record_init_type_mismatch(void) {
    const char *src =
        "type Point { let x: int; }\n"
        "func main() -> int {\n"
        "    let p = Point { x = true };\n"
        "    return 0;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_duplicate_function(void) {
    const char *src =
        "func main() -> int { return 0; }\n"
        "func main() -> int { return 1; }\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_duplicate_record(void) {
    const char *src =
        "type P { let x: int; }\n"
        "type P { let y: int; }\n"
        "func main() -> int { return 0; }\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_const_expression(void) {
    const char *src =
        "const BASE = 10;\n"
        "const DOUBLE = BASE * 2;\n"
        "func main() -> int {\n"
        "    return DOUBLE;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_const_forward_ref(void) {
    const char *src =
        "const A = B;\n"
        "const B = 1;\n"
        "func main() -> int { return 0; }\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_generic_valid(void) {
    const char *src =
        "func id<T>(x: T) -> T { return x; }\n"
        "func main() -> int {\n"
        "    return id(42);\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_generic_mismatch(void) {
    const char *src =
        "func pair<T>(a: T, b: T) -> [T] { return [a, b]; }\n"
        "func main() -> int {\n"
        "    let p = pair(1, 2);\n"
        "    return p[0];\n"
        "}\n"
        "func bad() -> int {\n"
        "    let q = pair(1, \"two\");\n"
        "    return 0;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_generic_arith_on_t(void) {
    const char *src =
        "func bad<T>(x: T) -> T { return x + 1; }\n"
        "func main() -> int { return 0; }\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_lambda_valid(void) {
    const char *src =
        "func make(n: int) -> fn(int) -> int {\n"
        "    return fn(x: int) -> int { return x + n; };\n"
        "}\n"
        "func main() -> int {\n"
        "    let f = make(10);\n"
        "    return f(5);\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_lambda_dynamic_call(void) {
    const char *src =
        "func main() -> int {\n"
        "    let fns = [fn(x: int) -> int { return x + 1; }];\n"
        "    return fns[0](5);\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_lambda_not_function(void) {
    const char *src =
        "func main() -> int {\n"
        "    let x = 5;\n"
        "    return x(3);\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_binop_type_mismatch_note(void) {
    const char *src =
        "func main() -> int {\n"
        "    return 1 + \"x\";\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(s_diag_has_errors(&d));
    bool saw_error = false;
    bool saw_note = false;
    for (s_size i = 0; i < d.count; ++i) {
        if (d.items[i].sev == S_DIAG_ERROR && d.items[i].code == 403)
            saw_error = true;
        if (d.items[i].sev == S_DIAG_NOTE &&
            strstr(d.items[i].msg, "right operand") != NULL)
            saw_note = true;
    }
    CHECK(saw_error);
    CHECK(saw_note);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static bool has_note_containing(const s_diag_ctx *d, const char *sub) {
    for (s_size i = 0; i < d->count; ++i) {
        if (d->items[i].sev == S_DIAG_NOTE &&
            strstr(d->items[i].msg, sub) != NULL)
            return true;
    }
    return false;
}

static void test_unknown_var_suggestion(void) {
    const char *src =
        "func main() -> int {\n"
        "    let counter = 5;\n"
        "    return countr;\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(has_note_containing(&d, "did you mean 'counter'"));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_unknown_func_suggestion(void) {
    const char *src =
        "func greets() -> int { return 1; }\n"
        "func main() -> int {\n"
        "    return greet();\n"
        "}\n";
    s_arena arena;
    s_diag_ctx d;
    s_module mod;
    bool ok = compile_str(src, &mod, &arena, &d);
    CHECK(!ok);
    CHECK(has_note_containing(&d, "did you mean 'greets'"));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

TEST_MAIN_BEGIN()
    test_valid_program();
    test_type_mismatch();
    test_unknown_var();
    test_missing_main();
    test_immutable_assign();
    test_arithmetic_type();
    test_array_literal_valid();
    test_array_literal_hetero();
    test_array_literal_empty();
    test_array_literal_empty_ascribed();
    test_array_any_elem();
    test_const_valid();
    test_const_type_mismatch();
    test_const_assign_rejected();
    test_const_expression();
    test_const_forward_ref();
    test_record_init_valid();
    test_record_init_missing_field();
    test_record_init_unknown_field();
    test_record_init_type_mismatch();
    test_duplicate_function();
    test_duplicate_record();
    test_generic_valid();
    test_generic_mismatch();
    test_generic_arith_on_t();
    test_lambda_valid();
    test_lambda_dynamic_call();
    test_lambda_not_function();
    test_binop_type_mismatch_note();
    test_unknown_var_suggestion();
    test_unknown_func_suggestion();
TEST_MAIN_END()
