#include "test_framework.h"
#include "suru/arena.h"
#include "suru/diag.h"
#include "suru/lexer.h"
#include "suru/token.h"
#include <string.h>

static void test_token_text_is_owned(void) {
    s_arena arena;
    s_diag_ctx diag;
    s_arena_init(&arena);
    s_diag_init(&diag);
    char identifier_source[] = "owned_identifier";
    s_lexer lexer;
    s_token token;
    s_lexer_init(&lexer, "ownership.suru", identifier_source,
                 sizeof(identifier_source) - 1u, &arena, &diag);
    CHECK(s_lexer_next(&lexer, &token));
    CHECK_EQ_INT(token.kind, S_TOK_IDENT);
    memset(identifier_source, 'x', sizeof(identifier_source) - 1u);
    CHECK(strcmp(token.v.s.text, "owned_identifier") == 0);
    char string_source[] = "\"owned literal\"";
    s_lexer_init(&lexer, "ownership.suru", string_source,
                 sizeof(string_source) - 1u, &arena, &diag);
    CHECK(s_lexer_next(&lexer, &token));
    CHECK_EQ_INT(token.kind, S_TOK_STR);
    memset(string_source, 'y', sizeof(string_source) - 1u);
    CHECK(strcmp(token.v.s.text, "owned literal") == 0);
    s_diag_dispose(&diag);
    s_arena_dispose(&arena);
}
TEST_MAIN_BEGIN()
    test_token_text_is_owned();
TEST_MAIN_END()
