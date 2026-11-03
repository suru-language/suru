#include "suru/io.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>

static bool file_size(FILE *f, s_size limit, s_size *out) {
    if (fseek(f, 0, SEEK_END) != 0) return false;
    long end = ftell(f);
    if (end < 0 || fseek(f, 0, SEEK_SET) != 0) return false;

    /* The cast is safe only after both bounds have been checked. */
    if ((uintmax_t)end > (uintmax_t)limit) {
        return false;
    }
    *out = (s_size)end;
    return true;
}

int s_io_read_all(const char *path, s_u8 **out, s_size *outlen,
                  s_size limit) {
    if (!path || !out || !outlen) return -1;
    if (limit == 0) limit = S_IO_MAX_FILE_SIZE;
    if (limit > S_IO_MAX_FILE_SIZE) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    s_size len = 0;
    if (!file_size(f, limit, &len)) {
        (void)fclose(f);
        return -1;
    }

    s_u8 *buf = (s_u8 *)malloc(len ? len : 1u);
    if (!buf) {
        (void)fclose(f);
        return -1;
    }

    s_size pos = 0;
    while (pos < len) {
        size_t want = (size_t)(len - pos);
        size_t got = fread(buf + pos, 1, want, f);
        if (got == 0) {
            free(buf);
            (void)fclose(f);
            return -1;
        }
        pos += (s_size)got;
    }

    int read_error = ferror(f) ? 1 : 0;
    int close_error = fclose(f) != 0 ? 1 : 0;
    if (read_error || close_error) {
        free(buf);
        return -1;
    }
    *out = buf;
    *outlen = len;
    return 0;
}

int s_io_write_all(const char *path, const void *data, s_size len) {
    if (!path || (len != 0 && !data) || len > S_IO_MAX_FILE_SIZE) return -1;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    const s_u8 *bytes = (const s_u8 *)data;
    s_size pos = 0;
    while (pos < len) {
        size_t want = (size_t)(len - pos);
        size_t wrote = fwrite(bytes + pos, 1, want, f);
        if (wrote == 0) {
            (void)fclose(f);
            return -1;
        }
        pos += (s_size)wrote;
    }

    int flush_error = fflush(f) != 0 ? 1 : 0;
    int close_error = fclose(f) != 0 ? 1 : 0;
    if (flush_error || close_error) return -1;
    return 0;
}
