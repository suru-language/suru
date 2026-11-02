#include "suru/driver.h"
#include "suru/bc.h"

#include <stdio.h>
#include <string.h>

static char *read_file(const char *path, s_size *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((s_size)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    s_size rd = fread(buf, 1, (s_size)sz, f);
    fclose(f);
    buf[rd] = '\0';
    *len = rd;
    return buf;
}

static int cmd_run(const char *path, int argc, char **argv) {
    s_size len;
    char *src = read_file(path, &len);
    if (!src) {
        fprintf(stderr, "suru: cannot read '%s'\n", path);
        return 1;
    }
    s_bc_module m;
    if (s_driver_compile(path, src, len, &m) != 0) {
        free(src);
        return 1;
    }
    free(src);
    int rc = s_driver_run_module_args(&m, argc, argv);
    s_bc_module_dispose(&m);
    return rc;
}

static int cmd_check(const char *path) {
    s_size len;
    char *src = read_file(path, &len);
    if (!src) {
        fprintf(stderr, "suru: cannot read '%s'\n", path);
        return 1;
    }
    s_bc_module m;
    int rc = s_driver_compile(path, src, len, &m);
    free(src);
    if (rc == 0) s_bc_module_dispose(&m);
    return rc == 0 ? 0 : 1;
}

static int cmd_compile(const char *path, const char *outpath) {
    s_size len;
    char *src = read_file(path, &len);
    if (!src) {
        fprintf(stderr, "suru: cannot read '%s'\n", path);
        return 1;
    }
    s_bc_module m;
    if (s_driver_compile(path, src, len, &m) != 0) {
        free(src);
        return 1;
    }
    free(src);
    s_u8 *buf = NULL;
    s_size buflen = 0;
    if (s_bc_write(&m, &buf, &buflen) != 0) {
        fprintf(stderr, "suru: serialization failed\n");
        s_bc_module_dispose(&m);
        return 1;
    }
    FILE *f = fopen(outpath, "wb");
    if (!f) {
        fprintf(stderr, "suru: cannot write '%s'\n", outpath);
        free(buf);
        s_bc_module_dispose(&m);
        return 1;
    }
    fwrite(buf, 1, buflen, f);
    fclose(f);
    free(buf);
    s_bc_module_dispose(&m);
    return 0;
}

static int cmd_exec(const char *path) {
    s_size len;
    char *buf = read_file(path, &len);
    if (!buf) {
        fprintf(stderr, "suru: cannot read '%s'\n", path);
        return 1;
    }
    s_bc_module m;
    if (s_bc_read((const s_u8 *)buf, len, &m) != 0) {
        fprintf(stderr, "suru: invalid bytecode module '%s'\n", path);
        free(buf);
        return 1;
    }
    free(buf);
    int rc = s_driver_run_module(&m);
    s_bc_module_dispose(&m);
    return rc;
}

static void usage(void) {
    fprintf(stderr,
            "usage: suru <run|check|compile|exec> <path> [args...]\n"
            "  run    <file.suru> [args...]   compile and run a source file\n"
            "  check  <file.suru>             compile and type-check only\n"
            "  compile <file.suru> <out.sbc>  compile to bytecode module\n"
            "  exec   <module.sbc> [args...]  run a precompiled bytecode module\n");
}

int main(int argc, char **argv) {
    if (argc == 2) {
        if (strcmp(argv[1], "--version") == 0 ||
            strcmp(argv[1], "-V") == 0) {
            printf("suru %s\n", "0.1.0");
            return 0;
        }
        if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0) {
            usage();
            return 0;
        }
    }
    if (argc < 3) {
        usage();
        return 2;
    }
    const char *cmd = argv[1];
    const char *path = argv[2];
    if (strcmp(cmd, "run") == 0) {
        return cmd_run(path, argc - 3, argv + 3);
    }
    if (strcmp(cmd, "check") == 0) {
        return cmd_check(path);
    }
    if (strcmp(cmd, "compile") == 0) {
        if (argc < 4) {
            usage();
            return 2;
        }
        return cmd_compile(path, argv[3]);
    }
    if (strcmp(cmd, "exec") == 0) {
        return cmd_exec(path);
    }
    usage();
    return 2;
}
