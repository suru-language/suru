#ifndef SURU_RUNTIME_H
#define SURU_RUNTIME_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host I/O and formatting helpers used by the VM's standard library. These
 * are intentionally isolated so the runtime does not depend on the compiler
 * frontend. */

void s_rt_write(const char *bytes, s_size len);
void s_rt_write_string(const char *s);
void s_rt_write_line(const char *bytes, s_size len);

/* Read one line (without trailing newline) from stdin. Returns a malloc'd,
 * NUL-terminated buffer (length in *out_len) or NULL on error/EOF (with
 * *out_len = 0). */
char *s_rt_read_line(s_size *out_len);

/* Format an i64 as decimal into buf (bufsz >= 24). Returns the length. */
s_size s_rt_format_i64(char *buf, s_size bufsz, s_i64 v);

/* Format a double with enough precision to round-trip. Returns length. */
s_size s_rt_format_f64(char *buf, s_size bufsz, double v);

#ifdef __cplusplus
}
#endif

#endif /* SURU_RUNTIME_H */
