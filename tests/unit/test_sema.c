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
TEST_MAIN_END()
