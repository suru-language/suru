#include "test_framework.h"
#include "suru/arena.h"
#include "suru/lexer.h"
#include "suru/parser.h"
#include "suru/ast.h"

static void test_tokens(void) {
    const char *src = "let x: int = 42; // comment\n"
                      "mut y = 1_000;\n"
                      "func f(a: float) -> float { return a * 2.0; }\n"
                      "type P { let l: int; let r: int; }\n"
                      "if x == 42 and y >= 100 { println(\"hi\\n\"); }\n";
    s_arena arena;
    s_arena_init(&arena);
    s_diag_ctx d;
    s_diag_init(&d);
    s_lexer lx;
    s_lexer_init(&lx, "t.suru", src, strlen(src), &arena, &d);
    int nident = 0, nint = 0, nkw = 0;
    s_token t;
    while (s_lexer_next(&lx, &t) && t.kind != S_TOK_EOF) {
        if (t.kind == S_TOK_IDENT) nident++;
        if (t.kind == S_TOK_INT) nint++;
        if (t.kind == S_TOK_KW_LET || t.kind == S_TOK_KW_MUT ||
            t.kind == S_TOK_KW_FUNC || t.kind == S_TOK_KW_TYPE) nkw++;
    }
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    CHECK(nint >= 3);
    CHECK(nident >= 8);
    CHECK_EQ_INT(nkw, 6);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_lex_error(void) {
    s_arena arena;
    s_arena_init(&arena);
    s_diag_ctx d;
    s_diag_init(&d);
    s_lexer lx;
    const char *src = "let x = 0x;"; /* hex with no digits -> error */
    s_lexer_init(&lx, "e.suru", src, strlen(src), &arena, &d);
    s_token t;
    while (s_lexer_next(&lx, &t) && t.kind != S_TOK_EOF) {}
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_parse_ok(void) {
    const char *src =
        "// a full program\n"
        "func add(a: int, b: int) -> int {\n"
        "    return a + b;\n"
        "}\n"
        "func main() -> int {\n"
        "    let x: int = add(20, 22);\n"
        "    return x;\n"
        "}\n";
    s_arena arena;
    s_arena_init(&arena);
    s_diag_ctx d;
    s_diag_init(&d);
    s_parser p;
    bool ok = s_parse(&p, "ok.suru", src, strlen(src), &arena, &d);
    CHECK(ok);
    CHECK_EQ_INT(s_diag_has_errors(&d), 0);
    CHECK_EQ_INT(p.mod.nfuncs, 2);
    CHECK_EQ_INT(p.mod.records, 0);
    CHECK_EQ_INT(p.mod.funcs[0].nparams, 2);
    CHECK_EQ_INT(p.mod.funcs[0].ret_type->kind, S_TYPE_INT);
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

static void test_parse_error(void) {
    const char *src = "func main() { let x = ; }";
    s_arena arena;
    s_arena_init(&arena);
    s_diag_ctx d;
    s_diag_init(&d);
    s_parser p;
    s_parse(&p, "bad.suru", src, strlen(src), &arena, &d);
    CHECK(s_diag_has_errors(&d));
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

TEST_MAIN_BEGIN()
    test_tokens();
    test_lex_error();
    test_parse_ok();
    test_parse_error();
TEST_MAIN_END()
