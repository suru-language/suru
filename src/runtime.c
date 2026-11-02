#include "suru/runtime.h"

#include <stdio.h>

void s_rt_write(const char *bytes, s_size len) {
    if (len) fwrite(bytes, 1, len, stdout);
}

void s_rt_write_string(const char *s) {
    fputs(s, stdout);
}

void s_rt_write_line(const char *bytes, s_size len) {
    if (len) fwrite(bytes, 1, len, stdout);
    fputc('\n', stdout);
}

char *s_rt_read_line(s_size *out_len) {
    size_t cap = 256;
    char *buf = malloc(cap);
    if (!buf) { *out_len = 0; return NULL; }
    size_t n = 0;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (c == '\n') break;
        if (n + 2 > cap) {
            size_t ncap = cap * 2;
            char *nb = realloc(buf, ncap);
            if (!nb) { free(buf); *out_len = 0; return NULL; }
            buf = nb;
            cap = ncap;
        }
        buf[n++] = (char)c;
    }
    if (n == 0 && c == EOF) {
        free(buf);
        *out_len = 0;
        return NULL;
    }
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

s_size s_rt_format_i64(char *buf, s_size bufsz, s_i64 v) {
    return (s_size)snprintf(buf, bufsz, "%lld", (long long)v);
}

s_size s_rt_format_f64(char *buf, s_size bufsz, double v) {
    if (v == (double)(s_i64)v && v >= -9.2e15 && v <= 9.2e15) {
        return (s_size)snprintf(buf, bufsz, "%lld", (long long)v);
    }
    return (s_size)snprintf(buf, bufsz, "%.17g", v);
}
