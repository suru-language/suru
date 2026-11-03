#ifndef SURU_IO_H
#define SURU_IO_H

#include "suru/base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The bootstrap compiler never reads an unbounded host file into memory. */
#define S_IO_MAX_FILE_SIZE ((s_size)256u * 1024u * 1024u)

/*
 * Read exactly one regular file into an owned byte buffer.  The returned
 * length is the exact number of bytes read; the buffer is not a C string and
 * may contain NUL bytes.  On failure, *out and *outlen are left untouched.
 * `limit` must be no greater than S_IO_MAX_FILE_SIZE; zero selects the
 * default limit.
 */
int s_io_read_all(const char *path, s_u8 **out, s_size *outlen,
                  s_size limit);

/* Write all bytes, retrying short writes.  A zero-length buffer is valid. */
int s_io_write_all(const char *path, const void *data, s_size len);

#ifdef __cplusplus
}
#endif

#endif /* SURU_IO_H */
