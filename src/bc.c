#include "suru/bc.h"

#include <string.h>

#define SU_BC_MAX_COUNT (1u << 24)

const s_bc_op_info s_bc_op_table[BC_OP_COUNT] = {
    /* op                         name            nop pops pushes br term */
    [BC_NOP]            = { "nop",            0, 0, 0, 0, 0 },
    [BC_CONST_INT]      = { "const_int",      1, 0, 1, 0, 0 },
    [BC_CONST_FLOAT]    = { "const_float",    1, 0, 1, 0, 0 },
    [BC_CONST_STR]      = { "const_str",      1, 0, 1, 0, 0 },
    [BC_CONST_NONE]     = { "const_none",    0, 0, 1, 0, 0 },
    [BC_LD_LOCAL]       = { "ld_local",       1, 0, 1, 0, 0 },
    [BC_ST_LOCAL]       = { "st_local",       1, 1, 0, 0, 0 },
    [BC_POP]            = { "pop",            0, 1, 0, 0, 0 },
    [BC_DUP]            = { "dup",            0, 0, 1, 0, 0 },
    [BC_SWAP]           = { "swap",           0, 0, 0, 0, 0 },
    [BC_NOT]            = { "not",            0, 1, 1, 0, 0 },
    [BC_NEG_I]         = { "neg_i",         0, 1, 1, 0, 0 },
    [BC_NEG_F]         = { "neg_f",         0, 1, 1, 0, 0 },
    [BC_CONV_I_F]      = { "conv_i_f",      0, 1, 1, 0, 0 },
    [BC_ADD_I]          = { "add_i",          0, 2, 1, 0, 0 },
    [BC_SUB_I]          = { "sub_i",          0, 2, 1, 0, 0 },
    [BC_MUL_I]          = { "mul_i",          0, 2, 1, 0, 0 },
    [BC_DIV_I]          = { "div_i",          0, 2, 1, 0, 0 },
    [BC_MOD_I]          = { "mod_i",          0, 2, 1, 0, 0 },
    [BC_ADD_F]         = { "add_f",         0, 2, 1, 0, 0 },
    [BC_SUB_F]         = { "sub_f",         0, 2, 1, 0, 0 },
    [BC_MUL_F]         = { "mul_f",         0, 2, 1, 0, 0 },
    [BC_DIV_F]         = { "div_f",         0, 2, 1, 0, 0 },
    [BC_BIT_AND]       = { "bit_and",       0, 2, 1, 0, 0 },
    [BC_BIT_OR]        = { "bit_or",        0, 2, 1, 0, 0 },
    [BC_BIT_XOR]       = { "bit_xor",       0, 2, 1, 0, 0 },
    [BC_SHL]           = { "shl",           0, 2, 1, 0, 0 },
    [BC_SHR]           = { "shr",           0, 2, 1, 0, 0 },
    [BC_BIT_NOT]       = { "bit_not",       0, 1, 1, 0, 0 },
    [BC_EQ_I]           = { "eq_i",           0, 2, 1, 0, 0 },
    [BC_NE_I]           = { "ne_i",           0, 2, 1, 0, 0 },
    [BC_LT_I]           = { "lt_i",           0, 2, 1, 0, 0 },
    [BC_LE_I]           = { "le_i",           0, 2, 1, 0, 0 },
    [BC_GT_I]           = { "gt_i",           0, 2, 1, 0, 0 },
    [BC_GE_I]           = { "ge_i",           0, 2, 1, 0, 0 },
    [BC_EQ_F]           = { "eq_f",           0, 2, 1, 0, 0 },
    [BC_NE_F]           = { "ne_f",           0, 2, 1, 0, 0 },
    [BC_LT_F]           = { "lt_f",           0, 2, 1, 0, 0 },
    [BC_LE_F]           = { "le_f",           0, 2, 1, 0, 0 },
    [BC_GT_F]           = { "gt_f",           0, 2, 1, 0, 0 },
    [BC_GE_F]           = { "ge_f",           0, 2, 1, 0, 0 },
    [BC_EQ_S]           = { "eq_s",           0, 2, 1, 0, 0 },
    [BC_NE_S]           = { "ne_s",           0, 2, 1, 0, 0 },
    [BC_LT_S]           = { "lt_s",           0, 2, 1, 0, 0 },
    [BC_LE_S]           = { "le_s",           0, 2, 1, 0, 0 },
    [BC_GT_S]           = { "gt_s",           0, 2, 1, 0, 0 },
    [BC_GE_S]           = { "ge_s",           0, 2, 1, 0, 0 },
    [BC_CONCAT_S]        = { "concat_s",       0, 2, 1, 0, 0 },
    [BC_REPEAT_S]        = { "repeat_s",       0, 2, 1, 0, 0 },
    [BC_EQ_B]           = { "eq_b",           0, 2, 1, 0, 0 },
    [BC_NE_B]           = { "ne_b",           0, 2, 1, 0, 0 },
    [BC_NEW_ARRAY]      = { "new_array",      1, 1, 1, 0, 0 }, /* pops variable */
    [BC_NEW_RECORD]     = { "new_record",     2, 1, 1, 0, 0 }, /* pops variable */
    [BC_IDX_LD]         = { "idx_ld",         0, 2, 1, 0, 0 },
    [BC_IDX_ST]         = { "idx_st",         0, 3, 0, 0, 0 },
    [BC_ARR_LEN]        = { "arr_len",        0, 1, 1, 0, 0 },
    [BC_FIELD_LD]       = { "field_ld",       2, 1, 1, 0, 0 },
    [BC_FIELD_ST]       = { "field_st",       2, 2, 0, 0, 0 },
    [BC_CALL]           = { "call",           1, 1, 1, 0, 0 }, /* pops variable */
    [BC_MKCLOSURE]     = { "mkclosure",      1, 0, 1, 0, 0 },
    [BC_MKCLOSURE_V]   = { "mkclosure_v",    2, 1, 1, 0, 0 }, /* pops variable */
    [BC_CALLV]          = { "call_v",         1, 1, 1, 0, 0 }, /* pops variable */
    [BC_BUILTIN]        = { "builtin",        1, 1, 1, 0, 0 }, /* pops variable */
    [BC_RET]            = { "ret",            0, 1, 0, 0, 1 },
    [BC_RET_UNIT]       = { "ret_unit",       0, 0, 0, 0, 1 },
    [BC_JMP]            = { "jmp",            1, 0, 0, 1, 0 },
    [BC_JZ]             = { "jz",             1, 1, 0, 1, 0 },
    [BC_JNZ]            = { "jnz",            1, 1, 0, 1, 0 },
    [BC_TRAP]           = { "trap",           0, 0, 0, 0, 1 },
};

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

void s_bc_module_init(s_bc_module *m) {
    memset(m, 0, sizeof(*m));
    m->version = S_BC_VERSION;
}

void s_bc_module_dispose(s_bc_module *m) {
    if (!m) return;
    free(m->ints);
    free(m->floats);
    if (m->strings) {
        for (s_u32 i = 0; i < m->nstrings; ++i) free(m->strings[i]);
        free(m->strings);
    }
    free(m->strlens);
    free(m->record_nfields);
    if (m->record_names) {
        for (s_u32 i = 0; i < m->nrecords; ++i) free((void *)m->record_names[i]);
        free(m->record_names);
    }
    if (m->funcs) {
        for (s_u32 i = 0; i < m->nfuncs; ++i) {
            free(m->funcs[i].name);
            free(m->funcs[i].ins);
        }
        free(m->funcs);
    }
    memset(m, 0, sizeof(*m));
}

/* ------------------------------------------------------------------ */
/* Serialization                                                       */
/* ------------------------------------------------------------------ */

typedef struct s_writer {
    s_u8 *buf;
    s_size len;
    s_size cap;
} s_writer;

static bool w_reserve(s_writer *w, s_size add) {
    if (w->len + add <= w->cap) return true;
    s_size ncap = w->cap ? w->cap : 256;
    while (ncap < w->len + add) ncap *= 2;
    s_u8 *n = realloc(w->buf, ncap);
    if (!n) return false;
    w->buf = n;
    w->cap = ncap;
    return true;
}

static bool w_u8(s_writer *w, s_u8 v) {
    if (!w_reserve(w, 1)) return false;
    w->buf[w->len++] = v;
    return true;
}
static bool w_u32(s_writer *w, s_u32 v) {
    if (!w_reserve(w, 4)) return false;
    w->buf[w->len++] = (s_u8)(v & 0xff);
    w->buf[w->len++] = (s_u8)((v >> 8) & 0xff);
    w->buf[w->len++] = (s_u8)((v >> 16) & 0xff);
    w->buf[w->len++] = (s_u8)((v >> 24) & 0xff);
    return true;
}
static bool w_i64(s_writer *w, s_i64 v) {
    for (int i = 0; i < 8; ++i) {
        if (!w_u8(w, (s_u8)((s_u64)v >> (8 * i)))) return false;
    }
    return true;
}
static bool w_f64(s_writer *w, double v) {
    s_u64 bits;
    memcpy(&bits, &v, 8);
    for (int i = 0; i < 8; ++i) {
        if (!w_u8(w, (s_u8)(bits >> (8 * i)))) return false;
    }
    return true;
}
static bool w_bytes(s_writer *w, const void *p, s_size n) {
    if (!w_reserve(w, n)) return false;
    memcpy(w->buf + w->len, p, n);
    w->len += n;
    return true;
}

typedef struct s_reader {
    const s_u8 *buf;
    s_size len;
    s_size pos;
    bool ok;
} s_reader;

static s_u8 r_u8(s_reader *r) {
    if (r->pos + 1 > r->len) { r->ok = false; return 0; }
    return r->buf[r->pos++];
}
static s_u32 r_u32(s_reader *r) {
    s_u32 v = 0;
    for (int i = 0; i < 4; ++i) {
        if (r->pos >= r->len) { r->ok = false; return 0; }
        v |= (s_u32)r->buf[r->pos++] << (8 * i);
    }
    return v;
}
static s_i64 r_i64(s_reader *r) {
    s_u64 v = 0;
    for (int i = 0; i < 8; ++i) {
        if (r->pos >= r->len) { r->ok = false; return 0; }
        v |= (s_u64)r->buf[r->pos++] << (8 * i);
    }
    return (s_i64)v;
}
static double r_f64(s_reader *r) {
    s_u64 bits = (s_u64)r_i64(r);
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

static bool r_bytes(s_reader *r, void *p, s_size n) {
    if (r->pos + n > r->len) { r->ok = false; return false; }
    memcpy(p, r->buf + r->pos, n);
    r->pos += n;
    return true;
}

int s_bc_write(const s_bc_module *m, s_u8 **out, s_size *outlen) {
    s_writer w = {0};
    if (!w_bytes(&w, S_BC_MAGIC, 4)) goto fail;
    if (!w_u32(&w, m->version)) goto fail;
    if (!w_u32(&w, m->entry)) goto fail;
    if (!w_u32(&w, m->nints)) goto fail;
    if (!w_u32(&w, m->nfloats)) goto fail;
    if (!w_u32(&w, m->nstrings)) goto fail;
    if (!w_u32(&w, m->nrecords)) goto fail;
    if (!w_u32(&w, m->nfuncs)) goto fail;

    for (s_u32 i = 0; i < m->nints; ++i)
        if (!w_i64(&w, m->ints[i])) goto fail;
    for (s_u32 i = 0; i < m->nfloats; ++i)
        if (!w_f64(&w, m->floats[i])) goto fail;
    for (s_u32 i = 0; i < m->nstrings; ++i) {
        if (!w_u32(&w, m->strlens[i])) goto fail;
        if (!w_bytes(&w, m->strings[i], m->strlens[i])) goto fail;
    }
    for (s_u32 i = 0; i < m->nrecords; ++i)
        if (!w_u32(&w, m->record_nfields[i])) goto fail;
    for (s_u32 i = 0; i < m->nfuncs; ++i) {
        const s_bc_func *f = &m->funcs[i];
        s_u32 namelen = (s_u32)strlen(f->name ? f->name : "");
        if (!w_u32(&w, namelen)) goto fail;
        if (!w_bytes(&w, f->name ? f->name : "", namelen)) goto fail;
        if (!w_u32(&w, f->nparams)) goto fail;
        if (!w_u32(&w, f->frame_size)) goto fail;
        if (!w_u32(&w, f->max_operand_depth)) goto fail;
        if (!w_u32(&w, f->nins)) goto fail;
        for (s_u32 j = 0; j < f->nins; ++j) {
            const s_bc_ins *in = &f->ins[j];
            if (!w_u8(&w, in->op)) goto fail;
            if (!w_u32(&w, in->a)) goto fail;
            if (!w_u32(&w, in->b)) goto fail;
            if (!w_u32(&w, in->c)) goto fail;
        }
    }
    *out = w.buf;
    *outlen = w.len;
    return 0;
fail:
    free(w.buf);
    return -1;
}

int s_bc_read(const s_u8 *buf, s_size buflen, s_bc_module *out) {
    s_reader r = { buf, buflen, 0, true };
    char magic[4];
    s_bc_module m;
    s_bc_module_init(&m);
    if (!r_bytes(&r, magic, 4)) goto fail;
    if (memcmp(magic, S_BC_MAGIC, 4) != 0) goto fail;
    m.version = r_u32(&r);
    if (m.version != S_BC_VERSION) goto fail;
    m.entry = r_u32(&r);
    m.nints = r_u32(&r);
    m.nfloats = r_u32(&r);
    m.nstrings = r_u32(&r);
    m.nrecords = r_u32(&r);
    m.nfuncs = r_u32(&r);
    if (!r.ok) goto fail;
    if (m.nints > SU_BC_MAX_COUNT || m.nfloats > SU_BC_MAX_COUNT ||
        m.nstrings > SU_BC_MAX_COUNT || m.nrecords > SU_BC_MAX_COUNT ||
        m.nfuncs > SU_BC_MAX_COUNT) goto fail;

    if (m.nints) {
        m.ints = malloc(sizeof(s_i64) * m.nints);
        if (!m.ints) goto fail;
        for (s_u32 i = 0; i < m.nints; ++i) m.ints[i] = r_i64(&r);
    }
    if (m.nfloats) {
        m.floats = malloc(sizeof(double) * m.nfloats);
        if (!m.floats) goto fail;
        for (s_u32 i = 0; i < m.nfloats; ++i) m.floats[i] = r_f64(&r);
    }
    if (m.nstrings) {
        m.strings = calloc(m.nstrings, sizeof(char *));
        m.strlens = malloc(sizeof(s_u32) * m.nstrings);
        if (!m.strings || !m.strlens) goto fail;
        for (s_u32 i = 0; i < m.nstrings; ++i) {
            s_u32 n = r_u32(&r);
            if (n > SU_BC_MAX_COUNT || !r.ok) goto fail;
            m.strlens[i] = n;
            char *s = malloc((s_size)n + 1);
            if (!s) goto fail;
            if (n && !r_bytes(&r, s, n)) { free(s); goto fail; }
            s[n] = '\0';
            m.strings[i] = s;
        }
    }
    if (m.nrecords) {
        m.record_nfields = malloc(sizeof(s_u32) * m.nrecords);
        m.record_names = calloc(m.nrecords, sizeof(char *));
        if (!m.record_nfields || !m.record_names) goto fail;
        for (s_u32 i = 0; i < m.nrecords; ++i) {
            m.record_nfields[i] = r_u32(&r);
            m.record_names[i] = NULL;
            if (!r.ok) goto fail;
        }
    }
    if (m.nfuncs) {
        m.funcs = calloc(m.nfuncs, sizeof(s_bc_func));
        if (!m.funcs) goto fail;
        for (s_u32 i = 0; i < m.nfuncs; ++i) {
            s_bc_func *f = &m.funcs[i];
            s_u32 namelen = r_u32(&r);
            if (namelen > SU_BC_MAX_COUNT || !r.ok) goto fail;
            f->name = malloc((s_size)namelen + 1);
            if (!f->name) goto fail;
            if (namelen && !r_bytes(&r, f->name, namelen)) goto fail;
            f->name[namelen] = '\0';
            f->nparams = r_u32(&r);
            f->frame_size = r_u32(&r);
            f->max_operand_depth = r_u32(&r);
            f->nins = r_u32(&r);
            if (!r.ok) goto fail;
            if (f->nins > SU_BC_MAX_COUNT) goto fail;
            if (f->nins) {
                f->ins = malloc(sizeof(s_bc_ins) * f->nins);
                if (!f->ins) goto fail;
                for (s_u32 j = 0; j < f->nins; ++j) {
                    s_bc_ins *in = &f->ins[j];
                    in->op = r_u8(&r);
                    in->a = r_u32(&r);
                    in->b = r_u32(&r);
                    in->c = r_u32(&r);
                    if (!r.ok) goto fail;
                }
            }
        }
    }
    if (!r.ok) goto fail;
    *out = m;
    return 0;
fail:
    s_bc_module_dispose(&m);
    return -1;
}
