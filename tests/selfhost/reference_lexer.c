#include "suru/arena.h"
#include "suru/lexer.h"

#include <stdio.h>
#include <stdlib.h>

static char *read_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END) != 0) return NULL;
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)size + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    int close_error = fclose(f) != 0;
    if (got != (size_t)size || close_error) { free(buf); return NULL; }
    buf[got] = '\0';
    *out_len = got;
    return buf;
}

static void print_string_value(const char *text, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        switch (c) {
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\0': fputs("\\0", stdout); break;
        default: putchar((int)c); break;
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    size_t len = 0;
    char *src = read_all(argv[1], &len);
    if (!src) return 2;

    s_arena arena;
    s_arena_init(&arena);
    s_diag_ctx diag;
    s_diag_init(&diag);
    s_lexer lexer;
    s_lexer_init(&lexer, argv[1], src, len, &arena, &diag);

    s_token token;
    while (s_lexer_next(&lexer, &token)) {
        printf("%d|%u|%u|", (int)token.kind, token.loc.line, token.loc.col);
        if (token.kind == S_TOK_IDENT) {
            printf("%.*s", (int)token.v.s.len, token.v.s.text);
        } else if (token.kind == S_TOK_INT) {
            printf("%lld", (long long)token.v.i);
        } else if (token.kind == S_TOK_FLOAT) {
            printf("%.17g", token.v.f);
        } else if (token.kind == S_TOK_STR) {
            print_string_value(token.v.s.text, token.v.s.len);
        }
        putchar('\n');
        if (token.kind == S_TOK_EOF) break;
    }
    int failed = s_diag_has_errors(&diag) ? 1 : 0;
    for (s_size i = 0; i < diag.count; ++i) {
        if (diag.items[i].sev == S_DIAG_ERROR) {
            printf("error|%u|%u\n", diag.items[i].loc.line, diag.items[i].loc.col);
        }
    }
    s_diag_dispose(&diag);
    s_arena_dispose(&arena);
    free(src);
    return failed;
}
