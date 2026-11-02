/* Fuzz target: feed arbitrary bytes to the bytecode deserializer and
 * verifier. Must never crash; the deserializer/verifier should either accept
 * or reject cleanly. Built under ASan/UBSan in CI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "suru/bc.h"
#include "suru/verify.h"

static void run_buffer(const unsigned char *buf, size_t n) {
    s_bc_module m;
    if (s_bc_read(buf, n, &m) == 0) {
        (void)s_verify_module(&m);
        s_bc_module_dispose(&m);
    }
}

int main(void) {
    size_t cap = 1 << 16;
    unsigned char *buf = malloc(cap);
    if (!buf) return 1;
    size_t len = fread(buf, 1, cap, stdin);
    /* feed many overlapping slices of different lengths */
    for (size_t start = 0; start < len; start += 7) {
        for (size_t size = 1; size < 256 && start + size <= len; size += 13) {
            run_buffer(buf + start, size);
        }
    }
    free(buf);
    return 0;
}
