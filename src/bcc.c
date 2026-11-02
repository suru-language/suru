#include "suru/bcc.h"
#include "suru/verify.h"
#include "suru/stdlib.h"

#include <string.h>

static char *xstrdup(const char *s) {
    s_size n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ------------------------------------------------------------------ */
/* Constant pool                                                       */
/* ------------------------------------------------------------------ */

typedef struct s_pool {
    s_i64 *ints;
    s_u32 nints, capints;
    double *floats;
    s_u32 nfloats, capf;
    char **strings;
    s_u32 *strlens;
    s_u32 nstrings, caps, capsl;
} s_pool;

#define POOL_RESERVE(arr, cap, esize, count) do {                        \
    if ((count) >= (cap)) {                                               \
        s_u32 ncap = (cap) ? (cap) * 2 : 8;                              \
        void *n = realloc((arr), (s_size)ncap * (esize));                 \
        if (!n) return false;                                             \
        (arr) = n;                                                        \
        (cap) = ncap;                                                     \
    }                                                                     \
} while (0)

static bool pool_add_int(s_pool *p, s_i64 v, s_u32 *out) {
    for (s_u32 i = 0; i < p->nints; ++i)
        if (p->ints[i] == v) { *out = i; return true; }
    POOL_RESERVE(p->ints, p->capints, sizeof(s_i64), p->nints + 1);
    p->ints[p->nints++] = v;
    *out = p->nints - 1;
    return true;
}

static bool pool_add_float(s_pool *p, double v, s_u32 *out) {
    for (s_u32 i = 0; i < p->nfloats; ++i)
        if (p->floats[i] == v) { *out = i; return true; }
    POOL_RESERVE(p->floats, p->capf, sizeof(double), p->nfloats + 1);
    p->floats[p->nfloats++] = v;
    *out = p->nfloats - 1;
    return true;
}

static bool pool_add_str(s_pool *p, const char *s, s_u32 len, s_u32 *out) {
    for (s_u32 i = 0; i < p->nstrings; ++i) {
        if (p->strlens[i] == len && memcmp(p->strings[i], s, len) == 0) {
            *out = i;
            return true;
        }
    }
    POOL_RESERVE(p->strings, p->caps, sizeof(char *), p->nstrings + 1);
    POOL_RESERVE(p->strlens, p->capsl, sizeof(s_u32), p->nstrings + 1);
    char *copy = malloc((s_size)len + 1);
    if (!copy) return false;
    memcpy(copy, s, len);
    copy[len] = '\0';
    p->strings[p->nstrings] = copy;
    p->strlens[p->nstrings] = len;
    p->nstrings++;
    *out = p->nstrings - 1;
    return true;
}

static void pool_free(s_pool *p) {
    for (s_u32 i = 0; i < p->nstrings; ++i) free(p->strings[i]);
    free(p->strings);
    free(p->strlens);
    free(p->ints);
    free(p->floats);
    memset(p, 0, sizeof(*p));
}

/* ------------------------------------------------------------------ */
/* Emitter                                                             */
/* ------------------------------------------------------------------ */

typedef struct s_emit {
    s_bc_ins *ins;
    s_u32 nins;
    s_u32 cap;
} s_emit;

static bool em_push(s_emit *e, s_u8 op, s_u32 a, s_u32 b, s_u32 c) {
    if (e->nins >= e->cap) {
        s_u32 ncap = e->cap ? e->cap * 2 : 64;
        s_bc_ins *n = realloc(e->ins, (s_size)ncap * sizeof(s_bc_ins));
        if (!n) return false;
        e->ins = n;
        e->cap = ncap;
    }
    s_bc_ins *in = &e->ins[e->nins++];
    in->op = op;
    in->a = a;
    in->b = b;
    in->c = c;
    return true;
}

#define EM(op) do { if (!em_push(&g->e, (op), 0, 0, 0)) return false; } while (0)
#define EM3(op, a, b, c) do { if (!em_push(&g->e, (op), (a), (b), (c))) return false; } while (0)

/* ------------------------------------------------------------------ */
/* Labels                                                              */
/* ------------------------------------------------------------------ */

#define LABEL_UNPLACED 0xFFFFFFFFu

typedef struct s_labelmgr {
    s_u32 *pos;
    s_u32 nlabels, caplabels;
    s_u32 *pins;      /* instruction indices awaiting patch */
    s_u32 *plab;
    s_u32 npatches, capp;
} s_labelmgr;

static s_u32 new_label(s_labelmgr *m) {
    if (m->nlabels >= m->caplabels) {
        s_u32 ncap = m->caplabels ? m->caplabels * 2 : 8;
        s_u32 *n = realloc(m->pos, (s_size)ncap * sizeof(s_u32));
        if (!n) return 0;
        m->pos = n;
        m->caplabels = ncap;
    }
    m->pos[m->nlabels] = LABEL_UNPLACED;
    return m->nlabels++;
}

static bool patch_label(s_labelmgr *m, s_u32 ins_idx, s_u32 lab) {
    if (m->npatches >= m->capp) {
        s_u32 ncap = m->capp ? m->capp * 2 : 16;
        s_u32 *ni = realloc(m->pins, (s_size)ncap * sizeof(s_u32));
        s_u32 *nl = realloc(m->plab, (s_size)ncap * sizeof(s_u32));
        if (!ni && !nl) return false;
        if (!nl) { free(ni); return false; }
        if (!ni) { free(nl); return false; }
        m->pins = ni;
        m->plab = nl;
        m->capp = ncap;
    }
    m->pins[m->npatches] = ins_idx;
    m->plab[m->npatches] = lab;
    m->npatches++;
    return true;
}

/* Emit a jump and record a patch so that when `lab` is placed, the jump
 * target (operand a) is filled in, */
static bool em_jump(s_emit *e, s_labelmgr *m, s_u8 op, s_u32 lab) {
    if (!em_push(e, op, 0, 0, 0)) return false;
    return patch_label(m, e->nins - 1, lab);
}

static void place_label(s_emit *e, s_labelmgr *m, s_u32 lab) {
    m->pos[lab] = e->nins;
    for (s_u32 i = 0; i < m->npatches; ++i) {
        if (m->plab[i] == lab) {
            e->ins[m->pins[i]].a = e->nins;
        }
    }
}

/* Resolve any patches whose label was placed after the patch was recorded
 * (backward jumps). Must be called after all emission for a function. */
static void finalize_labels(s_emit *e, s_labelmgr *m) {
    for (s_u32 i = 0; i < m->npatches; ++i) {
        s_u32 lab = m->plab[i];
        if (m->pos[lab] != LABEL_UNPLACED) {
            e->ins[m->pins[i]].a = m->pos[lab];
        }
    }
}

/* ------------------------------------------------------------------ */
/* Generator context                                                   */
/* ------------------------------------------------------------------ */

typedef enum s_call_kind { S_CALL_FUNC, S_CALL_BUILTIN, S_CALL_CONSTRUCTOR } s_call_kind;

typedef struct s_loopctx {
    s_u32 break_label;
    s_u32 continue_label;
} s_loopctx;

typedef struct s_gen {
    const s_module *ast;
    s_arena *arena;
    s_diag_ctx *diag;
    s_pool pool;
    s_emit e;
    s_labelmgr labels;
    const s_func_decl *fn;
    s_u32 frame_max;
    bool ok;
} s_gen;

static bool em_const_int(s_gen *g, s_i64 v) {
    s_u32 ci;
    if (!pool_add_int(&g->pool, v, &ci)) return false;
    EM3(BC_CONST_INT, ci, 0, 0);
    return true;
}

static int resolve_func_index(const s_module *ast, const char *name) {
    for (s_size i = 0; i < ast->nfuncs; ++i)
        if (strcmp(ast->funcs[i].name, name) == 0) return (int)i;
    return -1;
}

static int resolve_record_index(const s_module *ast, const char *name) {
    if (!name) return -1;
    for (s_size i = 0; i < ast->nrecords; ++i)
        if (strcmp(ast->records[i].name, name) == 0) return (int)i;
    return -1;
}

static void classify_call(const s_module *ast, const char *name,
                          s_call_kind *kind, int *target) {
    int f = resolve_func_index(ast, name);
    if (f >= 0) { *kind = S_CALL_FUNC; *target = f; return; }
    const s_builtin_sig *bi = s_builtin_lookup(name);
    if (bi) {
        *kind = S_CALL_BUILTIN;
        *target = (int)(bi - s_builtin_table);
        return;
    }
    int r = resolve_record_index(ast, name);
    if (r >= 0) { *kind = S_CALL_CONSTRUCTOR; *target = r; return; }
    *kind = S_CALL_FUNC;
    *target = -1;
}

static bool gen_expr(s_gen *g, const s_expr *e);
static bool gen_stmt(s_gen *g, const s_stmt *s, const s_loopctx *loop);

static bool push_bin_op(s_gen *g, const s_expr *e) {
    const s_type *lt = e->u.binary.lhs->type;
    bool is_float = lt && lt->kind == S_TYPE_FLOAT;
    bool is_str = lt && lt->kind == S_TYPE_STR;
    switch (e->u.binary.op) {
    case S_BIN_ADD: EM(is_str ? BC_CONCAT_S : is_float ? BC_ADD_F : BC_ADD_I); break;
    case S_BIN_SUB: EM(is_float ? BC_SUB_F : BC_SUB_I); break;
    case S_BIN_MUL: EM(is_float ? BC_MUL_F : BC_MUL_I); break;
    case S_BIN_DIV: EM(is_float ? BC_DIV_F : BC_DIV_I); break;
    case S_BIN_MOD: EM(BC_MOD_I); break;
    case S_BIN_EQ: EM(is_float ? BC_EQ_F : is_str ? BC_EQ_S : BC_EQ_I); break;
    case S_BIN_NE: EM(is_float ? BC_NE_F : is_str ? BC_NE_S : BC_NE_I); break;
    case S_BIN_LT: EM(is_float ? BC_LT_F : is_str ? BC_LT_S : BC_LT_I); break;
    case S_BIN_LE: EM(is_float ? BC_LE_F : is_str ? BC_LE_S : BC_LE_I); break;
    case S_BIN_GT: EM(is_float ? BC_GT_F : is_str ? BC_GT_S : BC_GT_I); break;
    case S_BIN_GE: EM(is_float ? BC_GE_F : is_str ? BC_GE_S : BC_GE_I); break;
    default: return false;
    }
    return true;
}

static bool gen_call(s_gen *g, const s_expr *e) {
    const s_expr *callee = e->u.call.callee;
    s_call_kind kind;
    int target;

    if (callee->kind == S_EXPR_FUNC_REF) {
        kind = S_CALL_FUNC;
        target = callee->u.var.var_index;
    } else if (callee->kind == S_EXPR_VAR) {
        classify_call(g->ast, callee->u.var.name, &kind, &target);
    } else {
        /* dynamic call through a function-typed value */
        if (!gen_expr(g, callee)) return false;
        for (s_size i = 0; i < e->u.call.nargs; ++i) {
            if (!gen_expr(g, e->u.call.args[i])) return false;
        }
        EM3(BC_CALLV, (s_u32)e->u.call.nargs, 0, 0);
        return true;
    }

    for (s_size i = 0; i < e->u.call.nargs; ++i) {
        if (!gen_expr(g, e->u.call.args[i])) return false;
    }
    if (kind == S_CALL_FUNC) {
        if (target < 0) return false;
        EM3(BC_CALL, (s_u32)target, 0, 0);
    } else if (kind == S_CALL_BUILTIN) {
        EM3(BC_BUILTIN, (s_u32)target, 0, 0);
    } else {
        EM3(BC_NEW_RECORD, (s_u32)target,
             (s_u32)g->ast->records[target].nfields, 0);
    }
    return true;
}

static bool gen_assign(s_gen *g, const s_expr *e, bool compound) {
    const s_expr *lhs, *rhs;
    if (e->kind == S_EXPR_COMPOUND_ASSIGN) {
        lhs = e->u.compound.lhs;
        rhs = e->u.compound.rhs;
    } else {
        lhs = e->u.assign.lhs;
        rhs = e->u.assign.rhs;
    }
    switch (lhs->kind) {
    case S_EXPR_VAR: {
        s_u32 slot = (s_u32)lhs->u.var.var_index;
        if (compound) {
            EM3(BC_LD_LOCAL, slot, 0, 0);
            if (!gen_expr(g, rhs)) return false;
            if (!push_bin_op(g, e)) return false;
            EM3(BC_ST_LOCAL, slot, 0, 0);
        } else {
            if (!gen_expr(g, rhs)) return false;
            EM3(BC_ST_LOCAL, slot, 0, 0);
        }
        return true;
    }
    case S_EXPR_INDEX: {
        if (compound) {
            if (!gen_expr(g, lhs->u.index.obj)) return false;
            EM(BC_DUP);
            if (!gen_expr(g, lhs->u.index.index)) return false;
            EM(BC_DUP);
            EM(BC_IDX_LD);
            if (!gen_expr(g, rhs)) return false;
            if (!push_bin_op(g, e)) return false;
            EM(BC_IDX_ST);
            return true;
        }
        if (!gen_expr(g, lhs->u.index.obj)) return false;
        if (!gen_expr(g, lhs->u.index.index)) return false;
        if (!gen_expr(g, rhs)) return false;
        EM(BC_IDX_ST);
        return true;
    }
    case S_EXPR_FIELD: {
        s_u32 rec = (s_u32)lhs->type->record_index;
        s_u32 fid = (s_u32)lhs->u.field.field_index;
        if (compound) {
            if (!gen_expr(g, lhs->u.field.obj)) return false;
            EM(BC_DUP);
            EM3(BC_FIELD_LD, rec, fid, 0);
            if (!gen_expr(g, rhs)) return false;
            if (!push_bin_op(g, e)) return false;
            EM3(BC_FIELD_ST, rec, fid, 0);
            return true;
        }
        if (!gen_expr(g, lhs->u.field.obj)) return false;
        if (!gen_expr(g, rhs)) return false;
        EM3(BC_FIELD_ST, rec, fid, 0);
        return true;
    }
    default:
        return false;
    }
}

static bool gen_expr(s_gen *g, const s_expr *e) {
    switch (e->kind) {
    case S_EXPR_INT_LIT: {
        s_u32 ci;
        if (!pool_add_int(&g->pool, e->u.i, &ci)) return false;
        EM3(BC_CONST_INT, ci, 0, 0);
        return true;
    }
    case S_EXPR_FLOAT_LIT: {
        s_u32 cf;
        if (!pool_add_float(&g->pool, e->u.f, &cf)) return false;
        EM3(BC_CONST_FLOAT, cf, 0, 0);
        return true;
    }
    case S_EXPR_STR_LIT: {
        s_u32 cs;
        if (!pool_add_str(&g->pool, e->u.str.text, (s_u32)e->u.str.len, &cs))
            return false;
        EM3(BC_CONST_STR, cs, 0, 0);
        return true;
    }
    case S_EXPR_BOOL_LIT: {
        s_u32 ci;
        if (!pool_add_int(&g->pool, e->u.b ? 1 : 0, &ci)) return false;
        EM3(BC_CONST_INT, ci, 0, 0);
        return true;
    }
    case S_EXPR_NONE_LIT:
        EM(BC_CONST_NONE);
        return true;
    case S_EXPR_ARRAY_LIT: {
        for (s_size i = 0; i < e->u.array.count; ++i) {
            if (!gen_expr(g, e->u.array.elems[i])) return false;
        }
        EM3(BC_NEW_ARRAY, (s_u32)e->u.array.count, 0, 0);
        return true;
    }
    case S_EXPR_VAR:
        EM3(BC_LD_LOCAL, (s_u32)e->u.var.var_index, 0, 0);
        return true;
    case S_EXPR_FUNC_REF:
        EM3(BC_MKCLOSURE, (s_u32)e->u.var.var_index, 0, 0);
        return true;
    case S_EXPR_CALL:
        return gen_call(g, e);
    case S_EXPR_INDEX:
        if (!gen_expr(g, e->u.index.obj)) return false;
        if (!gen_expr(g, e->u.index.index)) return false;
        EM(BC_IDX_LD);
        return true;
    case S_EXPR_FIELD: {
        s_u32 rec = (s_u32)e->type->record_index;
        s_u32 fid = (s_u32)e->u.field.field_index;
        if (!gen_expr(g, e->u.field.obj)) return false;
        EM3(BC_FIELD_LD, rec, fid, 0);
        return true;
    }
    case S_EXPR_UNARY: {
        if (!gen_expr(g, e->u.unary.operand)) return false;
        if (e->u.unary.op == S_UN_NEG) {
            const s_type *t = e->u.unary.operand->type;
            EM(t && t->kind == S_TYPE_FLOAT ? BC_NEG_F : BC_NEG_I);
        } else {
            EM(BC_NOT);
        }
        return true;
    }
    case S_EXPR_BINARY: {
        s_bin_op op = e->u.binary.op;
        if (op == S_BIN_AND || op == S_BIN_OR) {
            s_u32 lfalse = new_label(&g->labels);
            s_u32 ldone = new_label(&g->labels);
            if (!gen_expr(g, e->u.binary.lhs)) return false;
            if (op == S_BIN_AND) {
                if (!em_jump(&g->e, &g->labels, BC_JZ, lfalse)) return false;
                if (!gen_expr(g, e->u.binary.rhs)) return false;
                if (!em_jump(&g->e, &g->labels, BC_JMP, ldone)) return false;
                place_label(&g->e, &g->labels, lfalse);
                if (!em_const_int(g, 0)) return false;
                place_label(&g->e, &g->labels, ldone);
            } else {
                if (!em_jump(&g->e, &g->labels, BC_JNZ, ldone)) return false;
                if (!gen_expr(g, e->u.binary.rhs)) return false;
                if (!em_jump(&g->e, &g->labels, BC_JMP, ldone)) return false;
                place_label(&g->e, &g->labels, ldone);
            }
            return true;
        }
        if (!gen_expr(g, e->u.binary.lhs)) return false;
        if (!gen_expr(g, e->u.binary.rhs)) return false;
        return push_bin_op(g, e);
    }
    case S_EXPR_ASSIGN:
        return gen_assign(g, e, false);
    case S_EXPR_COMPOUND_ASSIGN:
        return gen_assign(g, e, true);
    }
    return false;
}

static bool gen_stmt(s_gen *g, const s_stmt *s, const s_loopctx *loop) {
    switch (s->kind) {
    case S_STMT_LET:
        if (!gen_expr(g, s->u.let.init)) return false;
        EM3(BC_ST_LOCAL, (s_u32)s->u.let.var_index, 0, 0);
        return true;
    case S_STMT_EXPR:
        if (!gen_expr(g, s->u.expr.expr)) return false;
        if (s->u.expr.expr->kind != S_EXPR_ASSIGN &&
            s->u.expr.expr->kind != S_EXPR_COMPOUND_ASSIGN) {
            EM(BC_POP);
        }
        return true;
    case S_STMT_RETURN:
        if (s->u.ret.has_expr) {
            if (!gen_expr(g, s->u.ret.expr)) return false;
            EM(BC_RET);
        } else {
            EM(BC_RET_UNIT);
        }
        return true;
    case S_STMT_IF: {
        s_u32 lelse = new_label(&g->labels);
        s_u32 lend = new_label(&g->labels);
        if (!gen_expr(g, s->u.ifstmt.cond)) return false;
        if (!em_jump(&g->e, &g->labels, BC_JZ, lelse)) return false;
        if (!gen_stmt(g, s->u.ifstmt.then_block, loop)) return false;
        if (!em_jump(&g->e, &g->labels, BC_JMP, lend)) return false;
        place_label(&g->e, &g->labels, lelse);
        if (s->u.ifstmt.else_block) {
            if (!gen_stmt(g, s->u.ifstmt.else_block, loop)) return false;
        }
        place_label(&g->e, &g->labels, lend);
        return true;
    }
    case S_STMT_WHILE: {
        s_u32 lloop = new_label(&g->labels);
        s_u32 lend = new_label(&g->labels);
        s_loopctx inner = { lend, lloop };
        place_label(&g->e, &g->labels, lloop);
        if (!gen_expr(g, s->u.whilestmt.cond)) return false;
        if (!em_jump(&g->e, &g->labels, BC_JZ, lend)) return false;
        if (!gen_stmt(g, s->u.whilestmt.body, &inner)) return false;
        if (!em_jump(&g->e, &g->labels, BC_JMP, lloop)) return false;
        place_label(&g->e, &g->labels, lend);
        return true;
    }
    case S_STMT_FOR: {
        s_u32 idx_slot = g->frame_max++;
        s_u32 arr_slot = g->frame_max++;
        s_u32 lloop = new_label(&g->labels);
        s_u32 lend = new_label(&g->labels);
        s_loopctx inner = { lend, lloop };
        if (!gen_expr(g, s->u.forstmt.iter)) return false;
        EM3(BC_ST_LOCAL, arr_slot, 0, 0);
        if (!em_const_int(g, 0)) return false;
        EM3(BC_ST_LOCAL, idx_slot, 0, 0);
        place_label(&g->e, &g->labels, lloop);
        EM3(BC_LD_LOCAL, idx_slot, 0, 0);
        EM3(BC_LD_LOCAL, arr_slot, 0, 0);
        EM(BC_ARR_LEN);
        EM(BC_LT_I);
        if (!em_jump(&g->e, &g->labels, BC_JZ, lend)) return false;
        EM3(BC_LD_LOCAL, arr_slot, 0, 0);
        EM3(BC_LD_LOCAL, idx_slot, 0, 0);
        EM(BC_IDX_LD);
        EM3(BC_ST_LOCAL, (s_u32)s->u.forstmt.var_index, 0, 0);
        if (!gen_stmt(g, s->u.forstmt.body, &inner)) return false;
        EM3(BC_LD_LOCAL, idx_slot, 0, 0);
        if (!em_const_int(g, 1)) return false;
        EM(BC_ADD_I);
        EM3(BC_ST_LOCAL, idx_slot, 0, 0);
        if (!em_jump(&g->e, &g->labels, BC_JMP, lloop)) return false;
        place_label(&g->e, &g->labels, lend);
        return true;
    }
    case S_STMT_BREAK:
        if (loop) {
            if (!em_jump(&g->e, &g->labels, BC_JMP, loop->break_label))
                return false;
        } else {
            EM(BC_TRAP);
        }
        return true;
    case S_STMT_CONTINUE:
        if (loop) {
            if (!em_jump(&g->e, &g->labels, BC_JMP, loop->continue_label))
                return false;
        } else {
            EM(BC_TRAP);
        }
        return true;
    case S_STMT_BLOCK:
        for (s_size i = 0; i < s->u.block.count; ++i) {
            if (!gen_stmt(g, s->u.block.items[i], loop)) return false;
        }
        return true;
    }
    return false;
}

static s_u32 max_user_slot(const s_stmt *s, s_u32 cur) {
    switch (s->kind) {
    case S_STMT_LET:
        if ((s_u32)s->u.let.var_index + 1 > cur)
            cur = (s_u32)s->u.let.var_index + 1;
        break;
    case S_STMT_FOR:
        if ((s_u32)s->u.forstmt.var_index + 1 > cur)
            cur = (s_u32)s->u.forstmt.var_index + 1;
        if (s->u.forstmt.body)
            cur = max_user_slot(s->u.forstmt.body, cur);
        break;
    case S_STMT_IF:
        cur = max_user_slot(s->u.ifstmt.then_block, cur);
        if (s->u.ifstmt.else_block)
            cur = max_user_slot(s->u.ifstmt.else_block, cur);
        break;
    case S_STMT_WHILE:
        if (s->u.whilestmt.body)
            cur = max_user_slot(s->u.whilestmt.body, cur);
        break;
    case S_STMT_BLOCK:
        for (s_size i = 0; i < s->u.block.count; ++i)
            cur = max_user_slot(s->u.block.items[i], cur);
        break;
    default:
        break;
    }
    return cur;
}

bool s_bcc_run(const s_module *ast, s_arena *arena, s_diag_ctx *diag,
               s_bc_module *out) {
    s_gen g;
    memset(&g, 0, sizeof(g));
    g.ast = ast;
    g.arena = arena;
    g.diag = diag;
    g.ok = true;
    s_bc_module_init(out);

    if (ast->nrecords) {
        out->record_nfields = malloc(sizeof(s_u32) * ast->nrecords);
        out->record_names = malloc(sizeof(char *) * ast->nrecords);
        if (!out->record_nfields || !out->record_names)
            return false;
        out->nrecords = (s_u32)ast->nrecords;
        for (s_size i = 0; i < ast->nrecords; ++i) {
            out->record_nfields[i] = (s_u32)ast->records[i].nfields;
            out->record_names[i] = xstrdup(ast->records[i].name);
        }
    }
    out->entry = (s_u32)ast->main_index;
    out->nfuncs = (s_u32)ast->nfuncs;
    if (ast->nfuncs) {
        out->funcs = calloc(ast->nfuncs, sizeof(s_bc_func));
        if (!out->funcs) { s_bc_module_dispose(out); return false; }
    }

    for (s_size f = 0; f < ast->nfuncs; ++f) {
        const s_func_decl *fn = &ast->funcs[f];
        s_bc_func *bf = &out->funcs[f];
        bf->name = xstrdup(fn->name);
        bf->nparams = (s_u32)fn->nparams;
        g.fn = fn;
        g.e.nins = 0;
        g.e.cap = 0;
        g.e.ins = NULL;
        g.labels.nlabels = 0;
        g.labels.npatches = 0;
        g.frame_max = max_user_slot(fn->body, (s_u32)fn->nparams);
        if (!gen_stmt(&g, fn->body, NULL)) {
            if (getenv("SURU_BCC_DEBUG"))
                fprintf(stderr, "[bcc] gen failed for '%s'\n", fn->name);
            g.ok = false;
        }
        if (g.ok && (g.e.nins == 0 ||
                     !s_bc_op_table[g.e.ins[g.e.nins - 1].op].is_terminal)) {
            s_type *rt = fn->ret_type;
            if (rt && rt->kind == S_TYPE_UNIT) {
                if (!em_push(&g.e, BC_RET_UNIT, 0, 0, 0)) g.ok = false;
            } else {
                if (!em_push(&g.e, BC_RET, 0, 0, 0)) g.ok = false;
            }
        }
        finalize_labels(&g.e, &g.labels);
        bf->frame_size = g.frame_max;
        bf->nins = g.e.nins;
        bf->ins = g.e.ins;
        /* Publish current pool sizes so the verifier can bound check indices
         * (the analysis only compares counts, not array contents). */
        out->nints = g.pool.nints;
        out->nfloats = g.pool.nfloats;
        out->nstrings = g.pool.nstrings;
        s_u32 max_depth = 0;
        if (s_verify_compute_func_depth(out, (s_u32)f, &max_depth) != 0) {
            if (getenv("SURU_BCC_DEBUG"))
                fprintf(stderr, "[bcc] depth/verify failed for '%s' (%u ins)\n",
                        fn->name, g.e.nins);
            g.ok = false;
        }
        bf->max_operand_depth = max_depth;
        free(g.labels.pos);
        free(g.labels.pins);
        free(g.labels.plab);
        g.labels.pos = NULL;
        g.labels.pins = NULL;
        g.labels.plab = NULL;
    }

    out->ints = g.pool.ints;
    out->nints = g.pool.nints;
    out->floats = g.pool.floats;
    out->nfloats = g.pool.nfloats;
    out->strings = g.pool.strings;
    out->strlens = g.pool.strlens;
    out->nstrings = g.pool.nstrings;
    memset(&g.pool, 0, sizeof(g.pool));

    if (!g.ok) {
        pool_free(&g.pool);
        s_bc_module_dispose(out);
        return false;
    }
    return true;
}
