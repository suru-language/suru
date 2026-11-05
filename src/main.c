#define _POSIX_C_SOURCE 200809L

#include "suru/driver.h"
#include "suru/bc.h"
#include "suru/io.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

static int cmd_compile_natural(const char *path, const char *outpath,
                               const char *argv0);

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

static int cmd_exec(const char *path);

static int cmd_run(const char *path, int argc, char **argv, int natural,
                   const char *argv0) {
    if (natural) {
        char out[512];
        snprintf(out, sizeof out, "/tmp/suru_nat_%d.sbc", (int)getpid());
        if (cmd_compile_natural(path, out, argv0) != 0) return 1;
        int rc = cmd_exec(out);
        remove(out);
        return rc;
    }
    (void)argc; (void)argv; (void)argv0;
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

static int resolve_driver(char *out, size_t n, const char *argv0,
                          const char *leaf) {
    const char *slash = strrchr(argv0, '/');
    char dir[1024];
    if (slash && slash != argv0) {
        size_t len = (size_t)(slash - argv0);
        snprintf(dir, sizeof dir, "%.*s", (int)len, argv0);
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    snprintf(out, n, "%s/../../tests/selfhost/%s", dir, leaf);
    if (access(out, R_OK) == 0) return 0;
    snprintf(out, n, "tests/selfhost/%s", leaf);
    if (access(out, R_OK) == 0) return 0;
    return -1;
}

static int bc_op_id(const char *name) {
    char norm[64];
    size_t k = 0;
    const char *p = name;
    if (strncmp(p, "BC_", 3) == 0) p += 3;
    for (; *p && k < sizeof(norm) - 1; ++p)
        norm[k++] = (char)tolower((unsigned char)*p);
    norm[k] = '\0';
    for (int i = 0; i < BC_OP_COUNT; ++i) {
        if (strcmp(s_bc_op_table[i].name, norm) == 0) return i;
    }
    return -1;
}

static int cmd_compile_natural(const char *path, const char *outpath,
                               const char *argv0) {
    char driver[2048];
    if (resolve_driver(driver, sizeof driver, argv0,
                       "natural_bytecode_driver.suru") != 0) {
        fprintf(stderr, "suru: natural bytecode driver not found\n");
        return 1;
    }
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "\"%s\" run \"%s\" \"%s\"", argv0, driver, path);
    FILE *p = popen(cmd, "r");
    if (!p) {
        fprintf(stderr, "suru: failed to run natural bytecode driver\n");
        return 1;
    }
    s_i64 ints[65536];
    double floats[65536];
    char *strs[65536];
    s_u32 strlens[65536];
    s_bc_ins ins[65536];
    s_u32 nints = 0, nfl = 0, nstr = 0, nins = 0;
    char line[4096];
    while (fgets(line, sizeof line, p)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "pool|int|", 9) == 0) {
            s_u32 idx = (s_u32)strtoul(line + 9, NULL, 10);
            const char *v = strchr(line + 9, '|');
            if (v && idx < 65536) { ints[idx] = strtoll(v + 1, NULL, 10); if (idx >= nints) nints = idx + 1; }
        } else if (strncmp(line, "pool|float|", 11) == 0) {
            s_u32 idx = (s_u32)strtoul(line + 11, NULL, 10);
            const char *v = strchr(line + 11, '|');
            if (v && idx < 65536) { floats[idx] = strtod(v + 1, NULL); if (idx >= nfl) nfl = idx + 1; }
        } else if (strncmp(line, "pool|str|", 9) == 0) {
            s_u32 idx = (s_u32)strtoul(line + 9, NULL, 10);
            const char *v = strchr(line + 9, '|');
            if (v && idx < 65536) {
                const char *s = v + 1;
                size_t len = strlen(s);
                if (len >= 2 && s[0] == '"' && s[len - 1] == '"') { s += 1; len -= 2; }
                strs[idx] = malloc(len + 1);
                if (strs[idx]) { memcpy(strs[idx], s, len); strs[idx][len] = '\0'; }
                strlens[idx] = (s_u32)len;
                if (idx >= nstr) nstr = idx + 1;
            }
        } else if (strncmp(line, "bc|", 3) == 0) {
            char *save = NULL;
            char *tok = strtok_r(line + 3, "|", &save);
            (void)tok; /* pc */
            tok = strtok_r(NULL, "|", &save);
            if (!tok) continue;
            int op = bc_op_id(tok);
            if (op < 0) continue;
            tok = strtok_r(NULL, "|", &save);
            const char *ops[3] = {"0", "0", "0"};
            int n = 0;
            while (tok && n < 3) { ops[n++] = tok; tok = strtok_r(NULL, "|", &save); }
            if (nins < 65536) {
                ins[nins].op = (s_u8)op;
                ins[nins].a = (s_u32)strtoul(ops[0], NULL, 10);
                ins[nins].b = (s_u32)strtoul(ops[1], NULL, 10);
                ins[nins].c = (s_u32)strtoul(ops[2], NULL, 10);
                nins++;
            }
        }
    }
    int pstatus = pclose(p);
    if (pstatus != 0) {
        fprintf(stderr, "suru: natural bytecode driver failed\n");
        return 1;
    }

    s_u32 frame_size = 0, depth = 0, max_depth = 0;
    for (s_u32 i = 0; i < nins; ++i) {
        const s_bc_op_info *oi = &s_bc_op_table[ins[i].op];
        if (ins[i].op == BC_LD_LOCAL || ins[i].op == BC_ST_LOCAL) {
            if (ins[i].a + 1 > frame_size) frame_size = ins[i].a + 1;
        }
        s_i64 d = depth - oi->pops + oi->pushes;
        if (d < 0) d = 0;
        depth = (s_u32)d;
        if (depth > max_depth) max_depth = depth;
    }
    if (frame_size == 0) frame_size = 1;

    s_bc_module m;
    s_bc_module_init(&m);
    m.version = S_BC_VERSION;
    m.entry = 0;
    m.nints = nints; m.ints = malloc(sizeof(s_i64) * (nints ? nints : 1));
    if (nints) memcpy(m.ints, ints, sizeof(s_i64) * nints);
    m.nfloats = nfl; m.floats = malloc(sizeof(double) * (nfl ? nfl : 1));
    if (nfl) memcpy(m.floats, floats, sizeof(double) * nfl);
    m.nstrings = nstr;
    m.strings = calloc(nstr ? nstr : 1, sizeof(char *));
    m.strlens = calloc(nstr ? nstr : 1, sizeof(s_u32));
    for (s_u32 i = 0; i < nstr; ++i) { m.strings[i] = strs[i]; m.strlens[i] = strlens[i]; }
    m.nfuncs = 1;
    m.funcs = calloc(1, sizeof(s_bc_func));
    m.funcs[0].name = malloc(5);
    if (m.funcs[0].name) memcpy(m.funcs[0].name, "main", 5);
    m.funcs[0].nparams = 0;
    m.funcs[0].frame_size = frame_size;
    m.funcs[0].max_operand_depth = max_depth;
    m.funcs[0].nins = nins;
    m.funcs[0].ins = malloc(sizeof(s_bc_ins) * (nins ? nins : 1));
    if (nins) memcpy(m.funcs[0].ins, ins, sizeof(s_bc_ins) * nins);

    s_u8 *buf = NULL;
    s_size buflen = 0;
    int rc = s_bc_write(&m, &buf, &buflen);
    s_bc_module_dispose(&m);
    if (rc != 0) { fprintf(stderr, "suru: natural module serialization failed\n"); return 1; }
    int wrc = s_io_write_all(outpath, buf, buflen);
    free(buf);
    if (wrc != 0) { fprintf(stderr, "suru: cannot write '%s'\n", outpath); return 1; }
    return 0;
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

static int cmd_compile(const char *path, const char *outpath, int natural,
                       const char *argv0) {
    if (natural) {
        return cmd_compile_natural(path, outpath, argv0);
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
        return cmd_run(path, argc - 3, argv + 3, natural, argv[0]);
    }
    if (strcmp(cmd, "check") == 0) {
        return cmd_check(path, natural, argv[0]);
    }
    if (strcmp(cmd, "compile") == 0) {
        if (argc < 4) {
            usage();
            return 2;
        }
        return cmd_compile(path, argv[3], natural, argv[0]);
    }
    if (strcmp(cmd, "exec") == 0) {
        return cmd_exec(path);
    }
    usage();
    return 2;
}
