#include "suru/verify.h"
#include "suru/stdlib.h"

#define VERIFY_OK 0
#define VERIFY_ERR -1

/* Pop/push counts for an instruction in context. Returns -1 on invalid op. */
static int ins_pops(const s_bc_module *m, s_u32 fid, const s_bc_ins *in) {
    (void)fid;
    if (in->op >= BC_OP_COUNT) return -1;
    switch ((int)in->op) {
    case BC_NEW_ARRAY: return (int)in->a;
    case BC_NEW_RECORD: return (int)in->b;
    case BC_MKCLOSURE_V: return (int)in->b;
    case BC_CALL:
        if (in->a >= m->nfuncs) return -1;
        return (int)m->funcs[in->a].nparams;
    case BC_CALLV:
        return (int)in->a + 1;
    case BC_BUILTIN:
        if (in->a >= S_BUILTIN_COUNT) return -1;
        return (int)s_builtin_table[in->a].nparams;
    default:
        return s_bc_op_table[in->op].pops;
    }
}

static int ins_pushes(const s_bc_module *m, s_u32 fid, const s_bc_ins *in) {
    (void)m; (void)fid;
    if (in->op >= BC_OP_COUNT) return -1;
    switch ((int)in->op) {
    case BC_NEW_ARRAY:
    case BC_NEW_RECORD:
    case BC_MKCLOSURE_V:
    case BC_CALL:
    case BC_CALLV:
        return 1;
    case BC_BUILTIN:
        /* Every builtin pushes a result; unit-returning builtins push none. */
        return 1;
    default:
        return s_bc_op_table[in->op].pushes;
    }
}

static int check_operands(const s_bc_module *m, s_u32 fid, s_u32 i) {
    const s_bc_func *f = &m->funcs[fid];
    const s_bc_ins *in = &f->ins[i];
    if (in->op >= BC_OP_COUNT) {
        if (getenv("SURU_VERIFY_DEBUG"))
            fprintf(stderr, "[verify] bad opcode %u at %u\n", in->op, i);
        return VERIFY_ERR;
    }
    switch ((int)in->op) {
    case BC_CONST_INT:
        if (in->a >= m->nints) {
            if (getenv("SURU_VERIFY_DEBUG"))
                fprintf(stderr, "[verify] const_int OOB at %u (a=%u nints=%u)\n",
                        i, in->a, m->nints);
            return VERIFY_ERR;
        }
        break;
    case BC_CONST_FLOAT: if (in->a >= m->nfloats) return VERIFY_ERR; break;
    case BC_CONST_STR: if (in->a >= m->nstrings) return VERIFY_ERR; break;
    case BC_LD_LOCAL:
    case BC_ST_LOCAL:
        if (in->a >= f->frame_size) {
            if (getenv("SURU_VERIFY_DEBUG"))
                fprintf(stderr, "[verify] local OOB at %u (a=%u frame=%u)\n",
                        i, in->a, f->frame_size);
            return VERIFY_ERR;
        }
        break;
    case BC_FIELD_LD:
    case BC_FIELD_ST:
        if (in->a >= m->nrecords) return VERIFY_ERR;
        if (in->b >= m->record_nfields[in->a]) return VERIFY_ERR;
        break;
    case BC_NEW_RECORD:
        if (in->a >= m->nrecords) return VERIFY_ERR;
        if (in->b != m->record_nfields[in->a]) return VERIFY_ERR;
        break;
    case BC_CALL:
        if (in->a >= m->nfuncs) return VERIFY_ERR;
        break;
    case BC_BUILTIN:
        if (in->a >= S_BUILTIN_COUNT) {
            if (getenv("SURU_VERIFY_DEBUG"))
                fprintf(stderr, "[verify] builtin OOB at %u (id=%u)\n", i, in->a);
            return VERIFY_ERR;
        }
        break;
    case BC_NEW_ARRAY:
        if (in->a > S_BC_MAX_DEPTH) return VERIFY_ERR;
        break;
    case BC_MKCLOSURE_V:
        if (in->a >= m->nfuncs) return VERIFY_ERR;
        if (in->b > S_BC_MAX_DEPTH) return VERIFY_ERR;
        break;
    case BC_JMP:
    case BC_JZ:
    case BC_JNZ:
        if (in->a >= f->nins) return VERIFY_ERR;
        break;
    case BC_PUSH_HANDLER:
        if (in->a >= f->nins) return VERIFY_ERR;
        if (in->b != UINT32_MAX && in->b >= m->nstrings) return VERIFY_ERR;
        break;
    default:
        break;
    }
    return VERIFY_OK;
}

int s_verify_compute_func_depth(const s_bc_module *m, s_u32 fid,
                                s_u32 *outmax) {
    if (!m || fid >= m->nfuncs) return VERIFY_ERR;
    const s_bc_func *f = &m->funcs[fid];
    const s_u32 n = f->nins;
    if (n == 0) return VERIFY_ERR;

    /* depth[i] = -1 unvisited, else operand depth reaching i */
    s_i64 *depth = malloc(sizeof(s_i64) * n);
    s_u32 *queue = malloc(sizeof(s_u32) * n);
    s_u32 qh = 0, qt = 0;
    if (!depth || !queue) { free(depth); free(queue); return VERIFY_ERR; }
    for (s_u32 i = 0; i < n; ++i) depth[i] = -1;

    s_u32 max = 0;
    bool reached_terminal = false;
    int err = VERIFY_OK;

    depth[0] = 0;
    queue[qt++] = 0;

    while (qh < qt && err == VERIFY_OK) {
        s_u32 u = queue[qh++];
        s_i64 d = depth[u];
        const s_bc_ins *in = &f->ins[u];
        if (check_operands(m, fid, u) != VERIFY_OK) { err = VERIFY_ERR; break; }
        int pops = ins_pops(m, fid, in);
        int pushes = ins_pushes(m, fid, in);
        if (pops < 0 || pushes < 0) {
            if (getenv("SURU_VERIFY_DEBUG"))
                fprintf(stderr, "[verify] bad pops/pushes at %u op=%u\n",
                        u, in->op);
            err = VERIFY_ERR; break;
        }
        if (d < pops) {
            if (getenv("SURU_VERIFY_DEBUG"))
                fprintf(stderr, "[verify] underflow at %u op=%s depth=%lld pops=%d\n",
                        u, s_bc_op_table[in->op].name, (long long)d, pops);
            err = VERIFY_ERR; break;
        }
        s_i64 d2 = d - pops + pushes;
        if (d2 > (s_i64)max) max = (s_u32)d2;

        const s_bc_op_info *info = &s_bc_op_table[in->op];
        if (info->is_terminal) {
            reached_terminal = true;
            continue;
        }
        if (info->is_branch) {
            s_u32 tgt = in->a;
            if (tgt >= n) {
                if (getenv("SURU_VERIFY_DEBUG"))
                    fprintf(stderr, "[verify] branch OOB at %u -> %u\n", u, tgt);
                err = VERIFY_ERR; break;
            }
            if (depth[tgt] < 0) { depth[tgt] = d2; queue[qt++] = tgt; }
            else if (depth[tgt] != d2) {
                if (getenv("SURU_VERIFY_DEBUG"))
                    fprintf(stderr, "[verify] merge mismatch tgt=%u got %lld have %lld\n",
                            tgt, (long long)d2, (long long)depth[tgt]);
                err = VERIFY_ERR; break;
            }
            if (in->op != BC_JMP) {
                s_u32 next = u + 1;
                if (next >= n) {
                    if (getenv("SURU_VERIFY_DEBUG"))
                        fprintf(stderr, "[verify] branch fallthrough off end at %u\n", u);
                    err = VERIFY_ERR; break;
                }
                if (depth[next] < 0) { depth[next] = d2; queue[qt++] = next; }
                else if (depth[next] != d2) {
                    if (getenv("SURU_VERIFY_DEBUG"))
                        fprintf(stderr, "[verify] merge mismatch fallthrough %u got %lld have %lld\n",
                                next, (long long)d2, (long long)depth[next]);
                    err = VERIFY_ERR; break;
                }
            }
        } else {
            s_u32 next = u + 1;
            if (next >= n) {
                if (getenv("SURU_VERIFY_DEBUG"))
                    fprintf(stderr, "[verify] fallthrough off end at %u\n", u);
                err = VERIFY_ERR; break;
            }
            if (depth[next] < 0) { depth[next] = d2; queue[qt++] = next; }
            else if (depth[next] != d2) {
                if (getenv("SURU_VERIFY_DEBUG"))
                    fprintf(stderr, "[verify] merge mismatch %u got %lld have %lld\n",
                            next, (long long)d2, (long long)depth[next]);
                err = VERIFY_ERR; break;
            }
        }
    }

    if (err == VERIFY_OK) {
        /* every reachable path must terminate */
        if (!reached_terminal) {
            if (getenv("SURU_VERIFY_DEBUG"))
                fprintf(stderr, "[verify] no terminal reached\n");
            err = VERIFY_ERR;
        }
        if (max > S_BC_MAX_DEPTH) err = VERIFY_ERR;
        else *outmax = max;
    }
    free(depth);
    free(queue);
    return err;
}

int s_verify_module(const s_bc_module *m) {
    if (!m) return VERIFY_ERR;
    if (m->version != S_BC_VERSION) return VERIFY_ERR;
    if (m->entry >= m->nfuncs) return VERIFY_ERR;
    for (s_u32 fid = 0; fid < m->nfuncs; ++fid) {
        const s_bc_func *f = &m->funcs[fid];
        if (f->frame_size < f->nparams) return VERIFY_ERR;
        if (f->frame_size > S_BC_MAX_FRAME) return VERIFY_ERR;
        s_u32 max_depth;
        if (s_verify_compute_func_depth(m, fid, &max_depth) != VERIFY_OK)
            return VERIFY_ERR;
        if (max_depth > f->max_operand_depth) return VERIFY_ERR;
    }
    return VERIFY_OK;
}
