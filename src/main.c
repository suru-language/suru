#include "suru/driver.h"
#include "suru/bc.h"
#include "suru/io.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char *read_file(const char *path, s_size *len) {
    s_u8 *bytes = NULL;
    s_size n = 0;
    if (s_io_read_all(path, &bytes, &n, S_IO_MAX_FILE_SIZE) != 0) return NULL;
    if (n == SIZE_MAX) {
        free(bytes);
        return NULL;
    }
    char *text = (char *)malloc(n + 1u);
    if (!text) {
        free(bytes);
        return NULL;
    }
    if (n != 0) memcpy(text, bytes, n);
    text[n] = '\0';
    free(bytes);
    *len = n;
    return text;
}

static int cmd_run(const char *path, int argc, char **argv, int natural) {
    if (natural) {
        fprintf(stderr, "suru: --syntax=natural is not yet implemented for 'run'\n");
        return 1;
    }
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

static int natural_frontend_path(char *out, size_t n, const char *argv0) {
    const char *slash = strrchr(argv0, '/');
    char dir[1024];
    if (slash && slash != argv0) {
        size_t len = (size_t)(slash - argv0);
        snprintf(dir, sizeof dir, "%.*s", (int)len, argv0);
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    snprintf(out, n, "%s/../../tests/selfhost/natural_frontend_driver.suru", dir);
    if (access(out, R_OK) == 0) return 0;
    snprintf(out, n, "tests/selfhost/natural_frontend_driver.suru");
    if (access(out, R_OK) == 0) return 0;
    return -1;
}

static int cmd_check(const char *path, int natural, const char *argv0) {
    if (natural) {
        char driver[2048];
        if (natural_frontend_path(driver, sizeof driver, argv0) != 0) {
            fprintf(stderr, "suru: --syntax=natural frontend driver not found\n");
            return 1;
        }
        char cmd[4096];
        snprintf(cmd, sizeof cmd, "\"%s\" run \"%s\" \"%s\" >/dev/null 2>&1",
                 argv0, driver, path);
        return system(cmd) == 0 ? 0 : 1;
    }
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

static int cmd_compile(const char *path, const char *outpath, int natural) {
    if (natural) {
        fprintf(stderr, "suru: --syntax=natural is not yet implemented for 'compile'\n");
        return 1;
    }
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
    if (s_io_write_all(outpath, buf, buflen) != 0) {
        fprintf(stderr, "suru: cannot write '%s'\n", outpath);
        free(buf);
        s_bc_module_dispose(&m);
        return 1;
    }
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
            "usage: suru [--syntax=legacy|natural] <run|check|compile|exec> <path> [args...]\n"
            "  run    <file.suru> [args...]   compile and run a source file\n"
            "  check  <file.suru>             compile and type-check only\n"
            "  compile <file.suru> <out.sbc>  compile to bytecode module\n"
            "  exec   <module.sbc> [args...]  run a precompiled bytecode module\n"
            "  --syntax selects the language edition; legacy is the default.\n");
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
    int natural = 0;
    int w = 1;
    for (int r = 1; r < argc; ++r) {
        if (strncmp(argv[r], "--syntax=", 9) == 0) {
            const char *v = argv[r] + 9;
            if (strcmp(v, "natural") == 0) natural = 1;
            else if (strcmp(v, "legacy") == 0) natural = 0;
            else {
                fprintf(stderr, "suru: unknown syntax '%s'\n", v);
                return 2;
            }
        } else {
            argv[w++] = argv[r];
        }
    }
    argc = w;

    if (argc < 3) {
        usage();
        return 2;
    }
    const char *cmd = argv[1];
    const char *path = argv[2];
    if (strcmp(cmd, "run") == 0) {
        return cmd_run(path, argc - 3, argv + 3, natural);
    }
    if (strcmp(cmd, "check") == 0) {
        return cmd_check(path, natural, argv[0]);
    }
    if (strcmp(cmd, "compile") == 0) {
        if (argc < 4) {
            usage();
            return 2;
        }
        return cmd_compile(path, argv[3], natural);
    }
    if (strcmp(cmd, "exec") == 0) {
        return cmd_exec(path);
    }
    usage();
    return 2;
}
