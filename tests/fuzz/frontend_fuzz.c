/* Fuzz target: feed arbitrary bytes as source text through lexer, parser,
 * semantic analysis, and bytecode compilation. Must never crash and must
 * report errors cleanly without leaking. Built under ASan/UBSan in CI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "suru/arena.h"
#include "suru/parser.h"
#include "suru/sema.h"
#include "suru/bcc.h"
#include "suru/bc.h"
#include "suru/verify.h"

static void run_source(const char *src, size_t len) {
    s_arena arena;
    s_arena_init(&arena);
    s_diag_ctx d;
    s_diag_init(&d);
    s_parser p;
    if (s_parse(&p, "fuzz.suru", src, len, &arena, &d)) {
        if (!s_diag_has_errors(&d)) {
            if (s_sema_run(&p.mod, &arena, &d)) {
                s_bc_module m;
                if (s_bcc_run(&p.mod, &arena, &d, &m)) {
                    (void)s_verify_module(&m); /* check optimizer output */
                    s_bc_module_dispose(&m);
                }
            }
        }
    }
    s_diag_dispose(&d);
    s_arena_dispose(&arena);
}

int main(void) {
    size_t cap = 1 << 14;
    char *buf = malloc(cap + 1);
    if (!buf) return 1;
    size_t len = fread(buf, 1, cap, stdin);
    buf[len] = '\0';
    /* try the whole input and a few truncated prefixes */
    run_source(buf, len);
    if (len > 64) run_source(buf, len / 2);
    if (len > 128) run_source(buf, len / 4);
    free(buf);
    return 0;
}
