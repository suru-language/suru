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

static int cmd_exec(const char *path, int argc, char **argv);

static int cmd_run(const char *path, int argc, char **argv, int natural,
                   const char *argv0) {
    if (natural) {
        char out[512];
        snprintf(out, sizeof out, "/tmp/suru_nat_%d.sbc", (int)getpid());
        if (cmd_compile_natural(path, out, argv0) != 0) return 1;
        int rc = cmd_exec(out, argc, argv);
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
    enum { MAX_FUNCS = 128, MAX_INS = 65536 };
    static char fnames[MAX_FUNCS][256];
    static s_bc_ins fins[MAX_FUNCS][MAX_INS];
    static s_u32 fnins[MAX_FUNCS];
    static s_u32 fnparams[MAX_FUNCS];
    static char *rnames[65536];
    static s_u32 rnfields[65536];
    s_u32 nints = 0, nfl = 0, nstr = 0, nrecords = 0;
    int nfuncs = 0, cur_func = -1;
    s_u32 cur_nins = 0;
    char line[4096];
    while (fgets(line, sizeof line, p)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "record|", 7) == 0) {
            s_u32 ridx = (s_u32)strtoul(line + 7, NULL, 10);
            char *save = NULL;
            strtok_r(line + 7, "|", &save); /* idx */
            const char *nm = strtok_r(NULL, "|", &save);
            char *nf = strtok_r(NULL, "|", &save);
            if (nm && nf && ridx < 65536) {
                rnames[ridx] = strdup(nm);
                rnfields[ridx] = (s_u32)strtoul(nf, NULL, 10);
                if (ridx + 1 > nrecords) nrecords = ridx + 1;
            }
        } else if (strncmp(line, "pool|int|", 9) == 0) {
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
        } else if (strncmp(line, "funcstart|", 10) == 0) {
            cur_func = nfuncs;
            if (cur_func < MAX_FUNCS) {
                char *save = NULL;
                char *ntok = strtok_r(line + 10, "|", &save);
                if (ntok) {
                    snprintf(fnames[cur_func], sizeof fnames[cur_func], "%s", ntok);
                    char *ptok = strtok_r(NULL, "|", &save);
                    fnparams[cur_func] = (s_u32)(ptok ? strtoul(ptok, NULL, 10) : 0);
                } else {
                    snprintf(fnames[cur_func], sizeof fnames[cur_func], "%s", line + 10);
                    fnparams[cur_func] = 0;
                }
                cur_nins = 0;
            }
        } else if (strncmp(line, "funcend|", 8) == 0) {
            if (cur_func >= 0 && cur_func < MAX_FUNCS) {
                fnins[cur_func] = cur_nins;
                nfuncs = cur_func + 1;
            }
            cur_func = -1;
        } else if (strncmp(line, "bc|", 3) == 0) {
            if (cur_func < 0 || cur_func >= MAX_FUNCS) continue;
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
            if (cur_nins < MAX_INS) {
                fins[cur_func][cur_nins].op = (s_u8)op;
                fins[cur_func][cur_nins].a = (s_u32)strtoul(ops[0], NULL, 10);
                fins[cur_func][cur_nins].b = (s_u32)strtoul(ops[1], NULL, 10);
                fins[cur_func][cur_nins].c = (s_u32)strtoul(ops[2], NULL, 10);
                cur_nins++;
            }
        }
    }
    int pstatus = pclose(p);
    if (pstatus != 0) {
        fprintf(stderr, "suru: natural bytecode driver failed\n");
        return 1;
    }
    if (getenv("SURU_NAT_DEBUG")) {
        for (int fi = 0; fi < nfuncs; ++fi)
            fprintf(stderr, "[nat] func %s nins=%u\n", fnames[fi], fnins[fi]);
    }
    if (nfuncs <= 0) {
        nfuncs = 1;
        snprintf(fnames[0], sizeof fnames[0], "main");
        fnins[0] = 0;
    }
    if (nfuncs > MAX_FUNCS) nfuncs = MAX_FUNCS;

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
    m.nrecords = nrecords;
    m.record_nfields = calloc(nrecords ? nrecords : 1, sizeof(s_u32));
    m.record_names = calloc(nrecords ? nrecords : 1, sizeof(const char *));
    for (s_u32 i = 0; i < nrecords; ++i) {
        m.record_nfields[i] = rnfields[i];
        m.record_names[i] = rnames[i];
    }
    m.nfuncs = (s_u32)nfuncs;
    m.funcs = calloc((size_t)nfuncs, sizeof(s_bc_func));
    m.entry = 0;
    for (int fi = 0; fi < nfuncs; ++fi) {
        if (strcmp(fnames[fi], "main") == 0) m.entry = (s_u32)fi;
    }
    for (int fi = 0; fi < nfuncs; ++fi) {
        s_u32 nins = fnins[fi];
        /* Remap the global slot ids used by this function to a dense local
         * 0-based space, so each function owns its own frame. Within a
         * function, parameters are bound before locals, so their global slot
         * ids are always smaller; sorting the used ids ascending therefore
         * preserves the parameter->slot contract (params occupy 0..nparams-1)
         * regardless of the order slots are referenced in the body. */
        s_u32 slotmap[MAX_INS];
        s_u32 nslots = 0;
        for (s_u32 i = 0; i < nins; ++i) {
            if (fins[fi][i].op == BC_LD_LOCAL || fins[fi][i].op == BC_ST_LOCAL) {
                s_u32 sid = fins[fi][i].a;
                s_u32 k = 0;
                while (k < nslots && slotmap[k] != sid) k++;
                if (k == nslots && nslots < MAX_INS) slotmap[nslots++] = sid;
            }
        }
        for (s_u32 i = 0; i < nslots; ++i) {
            for (s_u32 j = i + 1; j < nslots; ++j) {
                if (slotmap[j] < slotmap[i]) {
                    s_u32 t = slotmap[i]; slotmap[i] = slotmap[j]; slotmap[j] = t;
                }
            }
        }
        s_u32 frame_size = nslots < fnparams[fi] ? fnparams[fi] : nslots;
        for (s_u32 i = 0; i < nins; ++i) {
            if (fins[fi][i].op == BC_LD_LOCAL || fins[fi][i].op == BC_ST_LOCAL) {
                s_u32 sid = fins[fi][i].a;
                s_u32 k = 0;
                while (k < nslots && slotmap[k] != sid) k++;
                fins[fi][i].a = k;
            }
        }
        if (frame_size == 0) frame_size = 1;
        /* The verifier computes operand depth itself and only requires that the
         * actual depth never exceed max_operand_depth. Report a safe upper bound
         * rather than a fragile linear underestimate (variable-pop ops like
         * CALL/BUILTIN would otherwise undercount). */
        s_u32 max_depth = S_BC_MAX_DEPTH;
        m.funcs[fi].name = malloc(strlen(fnames[fi]) + 1);
        if (m.funcs[fi].name) strcpy(m.funcs[fi].name, fnames[fi]);
        m.funcs[fi].nparams = fnparams[fi];
        m.funcs[fi].frame_size = frame_size < fnparams[fi] ? fnparams[fi] : frame_size;
        m.funcs[fi].max_operand_depth = max_depth;
        m.funcs[fi].nins = nins;
        m.funcs[fi].ins = malloc(sizeof(s_bc_ins) * (nins ? nins : 1));
        if (nins) memcpy(m.funcs[fi].ins, fins[fi], sizeof(s_bc_ins) * nins);
    }

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

static int cmd_exec(const char *path, int argc, char **argv) {
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
    int rc = s_driver_run_module_args(&m, argc, argv);
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
        return cmd_exec(path, argc - 3, argv + 3);
    }
    usage();
    return 2;
}
