#include "suru/sema.h"
#include "suru/stdlib.h"

#include <limits.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Symbols and scopes                                                  */
/* ------------------------------------------------------------------ */

typedef struct s_sym {
    const char *name;
    s_type *type;
    int index;              /* frame slot */
    bool is_mut;
} s_sym;

typedef struct s_scope {
    struct s_scope *parent;
    s_sym *syms;
    s_size count;
    s_size cap;
    int depth;              /* lexical nesting depth (root = 0) */
} s_scope;

typedef struct s_sema {
    s_module *mod;
    s_arena *arena;
    s_diag_ctx *diag;
    s_scope *scope;         /* current scope */
    s_func_decl *fn;        /* current function (NULL outside) */
    int next_slot;
    int depth;
    s_expr *lambda_owner;   /* lambda being analyzed (capture collection) */
} s_sema;

/* ------------------------------------------------------------------ */
/* Scope helpers                                                       */
/* ------------------------------------------------------------------ */

static bool scope_push_sym(s_scope *sc, s_sym sym) {
    if (sc->count == sc->cap) {
        s_size ncap = sc->cap ? sc->cap * 2 : 8;
        s_sym *n = malloc(ncap * sizeof(s_sym));
        if (!n) return false;
        if (sc->syms) {
            memcpy(n, sc->syms, sc->count * sizeof(s_sym));
            free(sc->syms);
        }
        sc->syms = n;
        sc->cap = ncap;
    }
    sc->syms[sc->count++] = sym;
    return true;
}

static void scope_pop(s_scope *sc) {
    free(sc->syms);
}

/* ------------------------------------------------------------------ */
/* Name suggestions (did-you-mean)                                     */
/* ------------------------------------------------------------------ */

static int s_edit_dist(const char *a, const char *b) {
    size_t n = strlen(a);
    size_t m = strlen(b);
    if (n > 48 || m > 48) return INT_MAX;
    int row[49];
    for (size_t j = 0; j <= m; ++j) row[j] = (int)j;
    for (size_t i = 1; i <= n; ++i) {
        int prev = row[0];
        row[0] = (int)i;
        for (size_t j = 1; j <= m; ++j) {
            int cur = row[j];
            int mn = row[j - 1] + 1;
            int ins = row[j] + 1;
            int rep = prev + (a[i - 1] == b[j - 1] ? 0 : 1);
            if (ins < mn) mn = ins;
            if (rep < mn) mn = rep;
            row[j] = mn;
            prev = cur;
        }
    }
    return row[m];
}

static void update_suggestion(const char *name, const char *cand,
                              int *best, const char **bestc) {
    if (!cand || strcmp(cand, name) == 0) return;
    int d = s_edit_dist(name, cand);
    if (d < *best) { *best = d; *bestc = cand; }
}

static void maybe_note_suggestion(s_sema *S, s_source_loc loc,
                                  const char *name, int best, const char *bestc) {
    if (!bestc) return;
    size_t nlen = strlen(name);
    size_t clen = strlen(bestc);
    size_t ml = nlen < clen ? clen : nlen;
    int thresh = (int)(ml / 3 + 1);
    if (thresh < 2) thresh = 2;
    if (best > thresh) return;
    s_diag_note(S->diag, loc, 0, "did you mean '%s'?", bestc);
}

/* ------------------------------------------------------------------ */
/* Type utilities                                                      */
/* ------------------------------------------------------------------ */

static const char *type_kind_name(s_type_kind k) {
    switch (k) {
    case S_TYPE_INT: return "int";
    case S_TYPE_FLOAT: return "float";
    case S_TYPE_BOOL: return "bool";
    case S_TYPE_STR: return "str";
    case S_TYPE_UNIT: return "unit";
    case S_TYPE_ARRAY: return "[array]";
    case S_TYPE_FN: return "fn";
    case S_TYPE_RECORD: return "record";
    case S_TYPE_VOID: return "void";
    case S_TYPE_ANY: return "any";
    case S_TYPE_VAR: return "type parameter";
    }
    return "?";
}

static const char *type_name(s_type *t) {
    if (!t) return "?";
    switch (t->kind) {
    case S_TYPE_INT: return "int";
    case S_TYPE_FLOAT: return "float";
    case S_TYPE_BOOL: return "bool";
    case S_TYPE_STR: return "str";
    case S_TYPE_UNIT: return "unit";
    case S_TYPE_ANY: return "any";
    case S_TYPE_ARRAY: return "[array]";
    case S_TYPE_FN: return "fn";
    case S_TYPE_RECORD: return t->record_name ? t->record_name : "record";
    case S_TYPE_VOID: return "void";
    case S_TYPE_VAR: return "type parameter";
    }
    return "?";
}

static bool type_eq(s_type *a, s_type *b) {
    if (!a || !b) return false;
    if (a->kind == S_TYPE_ANY || b->kind == S_TYPE_ANY) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case S_TYPE_ARRAY:
        if (!a->elem || !b->elem) return true; /* wildcard element type */
        return type_eq(a->elem, b->elem);
    case S_TYPE_FN:
        if (a->nparams != b->nparams) return false;
        for (s_size i = 0; i < a->nparams; ++i)
            if (!type_eq(a->params[i], b->params[i])) return false;
        return type_eq(a->ret, b->ret);
    case S_TYPE_RECORD: return a->record_index == b->record_index;
    case S_TYPE_VAR: return a->var_index == b->var_index;
    default: return true;
    }
}

static s_type *type_unit(s_sema *S) {
    s_type *t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
    if (t) t->kind = S_TYPE_UNIT;
    return t;
}

/* ------------------------------------------------------------------ */
/* Record / function resolution                                        */
/* ------------------------------------------------------------------ */

static int resolve_record(s_sema *S, s_type *t, s_source_loc loc) {
    for (s_size i = 0; i < S->mod->nrecords; ++i) {
        if (strcmp(S->mod->records[i].name, t->record_name) == 0) {
            return (int)i;
        }
    }
    s_diag_error(S->diag, loc, 400, "unknown type '%s'", t->record_name);
    int best = INT_MAX;
    const char *bestc = NULL;
    for (s_size i = 0; i < S->mod->nrecords; ++i)
        update_suggestion(t->record_name, S->mod->records[i].name,
                          &best, &bestc);
    maybe_note_suggestion(S, loc, t->record_name, best, bestc);
    return -1;
}

static s_func_decl *resolve_function(s_sema *S, const char *name) {
    for (s_size i = 0; i < S->mod->nfuncs; ++i) {
        if (strcmp(S->mod->funcs[i].name, name) == 0)
            return &S->mod->funcs[i];
    }
    return NULL;
}

static int resolve_record_name(s_sema *S, const char *name) {
    for (s_size i = 0; i < S->mod->nrecords; ++i) {
        if (strcmp(S->mod->records[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static s_type *sig_to_type(s_sema *S, s_sig_type st) {
    s_type *t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
    if (!t) return NULL;
    switch (st) {
    case S_SIG_INT: t->kind = S_TYPE_INT; break;
    case S_SIG_FLOAT: t->kind = S_TYPE_FLOAT; break;
    case S_SIG_BOOL: t->kind = S_TYPE_BOOL; break;
    case S_SIG_STR: t->kind = S_TYPE_STR; break;
    case S_SIG_ARRAY_INT:
        t->kind = S_TYPE_ARRAY;
        t->elem = sig_to_type(S, S_SIG_INT);
        break;
    case S_SIG_ARRAY:
        t->kind = S_TYPE_ARRAY;
        t->elem = NULL; /* wildcard: matches any element type */
        break;
    case S_SIG_ARRAY_STR:
        t->kind = S_TYPE_ARRAY;
        t->elem = sig_to_type(S, S_SIG_STR);
        break;
    case S_SIG_ANY:
        t->kind = S_TYPE_ANY;
        break;
    default: t->kind = S_TYPE_UNIT; break;
    }
    return t;
}

static s_type *func_type(s_sema *S, const s_func_decl *f) {
    s_type *t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
    if (!t) return NULL;
    t->kind = S_TYPE_FN;
    t->nparams = f->nparams;
    t->ret = f->ret_type;
    t->params = s_arena_calloc(S->arena, f->nparams ? f->nparams : 1,
                               sizeof(s_type *), _Alignof(s_type *));
    if (!t->params) return NULL;
    for (s_size i = 0; i < f->nparams; ++i) {
        t->params[i] = f->params[i].type;
    }
    return t;
}

/* ------------------------------------------------------------------ */
/* Name resolution in scopes                                           */
/* ------------------------------------------------------------------ */

static s_sym *scope_lookup(s_scope *sc, const char *name, int *out_depth) {
    for (s_scope *s = sc; s; s = s->parent) {
        for (s_size i = s->count; i > 0; --i) {
            if (strcmp(s->syms[i - 1].name, name) == 0) {
                if (out_depth) *out_depth = s->depth;
                return &s->syms[i - 1];
            }
        }
    }
    if (out_depth) *out_depth = -1;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Expression checking                                                 */
/* ------------------------------------------------------------------ */

static s_type *check_expr(s_sema *S, s_expr *e);

static s_type *check_unary(s_sema *S, s_expr *e) {
    s_type *ot = check_expr(S, e->u.unary.operand);
    if (!ot) return type_unit(S);
    if (e->u.unary.op == S_UN_NOT) {
        if (ot->kind != S_TYPE_BOOL) {
            s_diag_error(S->diag, e->loc, 401,
                         "operator 'not' requires bool, got %s", type_name(ot));
            return type_unit(S);
        }
        return ot;
    }
    if (e->u.unary.op == S_UN_BITNOT) {
        if (ot->kind != S_TYPE_INT) {
            s_diag_error(S->diag, e->loc, 402,
                         "'~' requires an int operand, got %s", type_name(ot));
            return type_unit(S);
        }
        return ot;
    }
    if (ot->kind != S_TYPE_INT && ot->kind != S_TYPE_FLOAT) {
        s_diag_error(S->diag, e->loc, 402,
                     "unary '-' requires a numeric operand, got %s", type_name(ot));
        return type_unit(S);
    }
    return ot;
}

static s_type *check_binary(s_sema *S, s_expr *e) {
    s_type *lt = check_expr(S, e->u.binary.lhs);
    s_type *rt = check_expr(S, e->u.binary.rhs);
    if (!lt || !rt) return type_unit(S);

    switch (e->u.binary.op) {
    case S_BIN_ADD: case S_BIN_SUB: case S_BIN_MUL: case S_BIN_DIV:
        if (lt->kind == S_TYPE_INT && rt->kind == S_TYPE_INT) return lt;
        if (lt->kind == S_TYPE_FLOAT && rt->kind == S_TYPE_FLOAT) return lt;
        if (e->u.binary.op == S_BIN_ADD &&
            lt->kind == S_TYPE_STR && rt->kind == S_TYPE_STR) return lt;
        if ((lt->kind == S_TYPE_INT && rt->kind == S_TYPE_FLOAT) ||
            (lt->kind == S_TYPE_FLOAT && rt->kind == S_TYPE_INT)) {
            return lt->kind == S_TYPE_FLOAT ? lt : rt; /* int promotes to float */
        }
        if (e->u.binary.op == S_BIN_MUL &&
            ((lt->kind == S_TYPE_STR && rt->kind == S_TYPE_INT) ||
             (lt->kind == S_TYPE_INT && rt->kind == S_TYPE_STR))) {
            return lt->kind == S_TYPE_STR ? lt : rt; /* string repeat */
        }
        s_diag_error(S->diag, e->loc, 403,
                     "arithmetic operands must both be int or both float, got %s and %s",
                     type_name(lt), type_name(rt));
        s_diag_note(S->diag, e->u.binary.rhs->loc, 0,
                    "right operand has type %s", type_name(rt));
        return type_unit(S);
    case S_BIN_MOD:
        if (lt->kind == S_TYPE_INT && rt->kind == S_TYPE_INT) return lt;
        s_diag_error(S->diag, e->loc, 404,
                     "'%%' requires int operands, got %s and %s",
                     type_name(lt), type_name(rt));
        s_diag_note(S->diag, e->u.binary.rhs->loc, 0,
                    "right operand has type %s", type_name(rt));
        return type_unit(S);
    case S_BIN_EQ: case S_BIN_NE:
        if (!type_eq(lt, rt)) {
            s_diag_error(S->diag, e->loc, 405,
                         "cannot compare %s with %s", type_name(lt), type_name(rt));
            s_diag_note(S->diag, e->u.binary.rhs->loc, 0,
                        "right operand has type %s", type_name(rt));
            return type_unit(S);
        }
        break; /* comparison result is bool */
    case S_BIN_LT: case S_BIN_LE: case S_BIN_GT: case S_BIN_GE:
        if ((lt->kind == S_TYPE_INT && rt->kind == S_TYPE_FLOAT) ||
            (lt->kind == S_TYPE_FLOAT && rt->kind == S_TYPE_INT)) {
            break; /* int promotes to float; comparison result is bool */
        }
        if (!(lt->kind == S_TYPE_INT || lt->kind == S_TYPE_FLOAT ||
              lt->kind == S_TYPE_STR) || !type_eq(lt, rt)) {
            s_diag_error(S->diag, e->loc, 406,
                         "ordering requires comparable operands, got %s and %s",
                         type_name(lt), type_name(rt));
            s_diag_note(S->diag, e->u.binary.rhs->loc, 0,
                        "right operand has type %s", type_name(rt));
            return type_unit(S);
        }
        /* fallthrough */
        break;
    case S_BIN_BIT_AND: case S_BIN_BIT_OR: case S_BIN_BIT_XOR:
    case S_BIN_SHL: case S_BIN_SHR:
        if (lt->kind != S_TYPE_INT || rt->kind != S_TYPE_INT) {
            s_diag_error(S->diag, e->loc, 408,
                         "bitwise/shift operators require int operands, got %s and %s",
                         type_name(lt), type_name(rt));
            s_diag_note(S->diag, e->u.binary.rhs->loc, 0,
                        "right operand has type %s", type_name(rt));
            return type_unit(S);
        }
        return lt;
    case S_BIN_AND: case S_BIN_OR:
        if (lt->kind != S_TYPE_BOOL || rt->kind != S_TYPE_BOOL) {
            s_diag_error(S->diag, e->loc, 407,
                         "'and'/'or' require bool operands, got %s and %s",
                         type_name(lt), type_name(rt));
            s_diag_note(S->diag, e->u.binary.rhs->loc, 0,
                        "right operand has type %s", type_name(rt));
            return type_unit(S);
        }
        break;
    }

    /* bool result for comparisons/logic */
    s_type *b = type_unit(S);
    if (b) b->kind = S_TYPE_BOOL;
    return b;
}

static s_type *subst_type(s_sema *S, s_type *t, s_type **binding, int nb) {
    if (!t) return NULL;
    if (t->kind == S_TYPE_VAR) {
        int v = t->var_index;
        if (v >= 0 && v < nb && binding[v]) return binding[v];
        return t;
    }
    s_type *c = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
    if (!c) return NULL;
    *c = *t;
    c->elem = subst_type(S, t->elem, binding, nb);
    if (c->kind == S_TYPE_FN) {
        c->ret = subst_type(S, t->ret, binding, nb);
        for (s_size i = 0; i < c->nparams; ++i)
            c->params[i] = subst_type(S, t->params[i], binding, nb);
    }
    return c;
}

static bool unify_types(s_sema *S, s_type *conc, s_type *pat,
                        s_type **binding, int nb, s_expr *e) {
    (void)S; (void)e;
    if (!conc || !pat) return false;
    if (pat->kind == S_TYPE_VAR) {
        int v = pat->var_index;
        if (v < 0 || v >= nb) return false;
        if (binding[v]) return type_eq(binding[v], conc);
        binding[v] = conc;
        return true;
    }
    if (conc->kind != pat->kind) return false;
    switch (pat->kind) {
    case S_TYPE_ARRAY:
        return unify_types(S, conc->elem, pat->elem, binding, nb, e);
    case S_TYPE_FN: {
        if (conc->nparams != pat->nparams) return false;
        for (s_size i = 0; i < pat->nparams; ++i)
            if (!unify_types(S, conc->params[i], pat->params[i], binding, nb, e))
                return false;
        return unify_types(S, conc->ret, pat->ret, binding, nb, e);
    }
    case S_TYPE_RECORD:
        return conc->record_index == pat->record_index;
    default:
        return type_eq(conc, pat);
    }
}

static s_type *check_generic_call(s_sema *S, s_expr *e,
                                  const s_func_decl *f) {
    if (e->u.call.nargs != f->nparams) {
        s_diag_error(S->diag, e->loc, 412,
                     "function '%s' expects %zu argument(s), got %zu",
                     f->name, f->nparams, e->u.call.nargs);
        return type_unit(S);
    }
    s_type *binding[8];
    memset(binding, 0, sizeof(binding));
    int nb = f->ntype_params;
    for (s_size i = 0; i < f->nparams; ++i) {
        s_type *at = check_expr(S, e->u.call.args[i]);
        unify_types(S, at, f->params[i].type, binding, nb, e);
    }
    for (s_size i = 0; i < f->nparams; ++i) {
        s_type *at = e->u.call.args[i]->type;
        s_type *want = subst_type(S, f->params[i].type, binding, nb);
        if (at && want && !type_eq(at, want)) {
            s_diag_error(S->diag, e->u.call.args[i]->loc, 413,
                         "argument %zu of '%s' has type %s, expected %s",
                         i + 1, f->name, type_name(at), type_name(want));
        }
    }
    s_type *res = subst_type(S, f->ret_type, binding, nb);
    return res ? res : type_unit(S);
}

static s_type *check_call(s_sema *S, s_expr *e) {
    s_expr *callee = e->u.call.callee;
    const char *fname = NULL;
    if (callee->kind == S_EXPR_VAR) {
        fname = callee->u.var.name;
    }
    s_func_decl *f = fname ? resolve_function(S, fname) : NULL;
    if (f) {
        if (f->ntype_params > 0) return check_generic_call(S, e, f);
        if (e->u.call.nargs != f->nparams) {
            s_diag_error(S->diag, e->loc, 412,
                         "function '%s' expects %zu argument(s), got %zu",
                         fname, f->nparams, e->u.call.nargs);
            return type_unit(S);
        }
        for (s_size i = 0; i < f->nparams; ++i) {
            s_type *at = check_expr(S, e->u.call.args[i]);
            if (at && !type_eq(at, f->params[i].type)) {
                s_diag_error(S->diag, e->u.call.args[i]->loc, 413,
                             "argument %zu of '%s' has type %s, expected %s",
                             i + 1, fname, type_name(at), type_name(f->params[i].type));
            }
        }
        return f->ret_type;
    }

    const s_builtin_sig *bi = fname ? s_builtin_lookup(fname) : NULL;
    if (bi) {
        if (e->u.call.nargs != bi->nparams) {
            s_diag_error(S->diag, e->loc, 414,
                         "builtin '%s' expects %u argument(s), got %zu",
                         fname, bi->nparams, e->u.call.nargs);
            return type_unit(S);
        }
        for (s_size i = 0; i < bi->nparams; ++i) {
            s_type *at = check_expr(S, e->u.call.args[i]);
            s_type *want = sig_to_type(S, bi->params[i]);
            if (at && want && !type_eq(at, want)) {
                s_diag_error(S->diag, e->u.call.args[i]->loc, 415,
                             "argument %zu of '%s' has type %s, expected %s",
                             i + 1, fname, type_name(at), type_name(want));
            }
        }
        return sig_to_type(S, bi->ret);
    }

    {
        int ridx = fname ? resolve_record_name(S, fname) : -1;
        if (ridx >= 0) {
            s_record_decl *rec = &S->mod->records[ridx];
            if (e->u.call.nargs != rec->nfields) {
                s_diag_error(S->diag, e->loc, 416,
                             "record '%s' constructor expects %zu field(s), got %zu",
                             fname, rec->nfields, e->u.call.nargs);
                return type_unit(S);
            }
            for (s_size i = 0; i < rec->nfields; ++i) {
                s_type *at = check_expr(S, e->u.call.args[i]);
                if (at && !type_eq(at, rec->fields[i].type)) {
                    s_diag_error(S->diag, e->u.call.args[i]->loc, 417,
                                 "field %zu of '%s' has type %s, expected %s",
                                 i + 1, fname, type_name(at),
                                 type_name(rec->fields[i].type));
                }
            }
            s_type *rt = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
            if (rt) {
                rt->kind = S_TYPE_RECORD;
                rt->record_index = ridx;
                rt->record_name = rec->name;
            }
            return rt;
        }
    }

    /* Dynamic call through a function-typed value. */
    {
        s_type *ct = check_expr(S, callee);
        if (ct && ct->kind == S_TYPE_FN) {
            if (e->u.call.nargs != ct->nparams) {
                s_diag_error(S->diag, e->loc, 418,
                             "this function expects %zu argument(s), got %zu",
                             ct->nparams, e->u.call.nargs);
                return type_unit(S);
            }
            for (s_size i = 0; i < ct->nparams; ++i) {
                s_type *at = check_expr(S, e->u.call.args[i]);
                if (at && !type_eq(at, ct->params[i])) {
                    s_diag_error(S->diag, e->u.call.args[i]->loc, 419,
                                 "argument %zu has type %s, expected %s",
                                 i + 1, type_name(at), type_name(ct->params[i]));
                }
            }
            return ct->ret;
        }
    }

    s_diag_error(S->diag, e->loc, 411,
                 fname ? "unknown function '%s'" : "callee is not a function",
                 fname ? fname : "");
    if (fname) {
        int best = INT_MAX;
        const char *bestc = NULL;
        for (s_size i = 0; i < S->mod->nfuncs; ++i)
            update_suggestion(fname, S->mod->funcs[i].name, &best, &bestc);
        maybe_note_suggestion(S, e->loc, fname, best, bestc);
    }
    return type_unit(S);
}

static s_type *check_index(s_sema *S, s_expr *e) {
    s_type *ot = check_expr(S, e->u.index.obj);
    s_type *it = check_expr(S, e->u.index.index);
    if (it && it->kind != S_TYPE_INT) {
        s_diag_error(S->diag, e->u.index.index->loc, 414,
                     "array index must be int, got %s", type_name(it));
    }
    if (!ot) return type_unit(S);
    if (ot->kind == S_TYPE_STR) {
        /* string indexing yields a one-character string */
        e->is_lvalue = false;
        return ot;
    }
    if (ot->kind != S_TYPE_ARRAY) {
        s_diag_error(S->diag, e->loc, 415,
                     "cannot index a value of type %s", type_name(ot));
        return type_unit(S);
    }
    e->is_lvalue = true;
    return ot->elem;
}

static s_type *check_field(s_sema *S, s_expr *e) {
    s_type *ot = check_expr(S, e->u.field.obj);
    if (!ot) return type_unit(S);
    if (ot->kind != S_TYPE_RECORD || ot->record_index < 0) {
        s_diag_error(S->diag, e->loc, 416,
                     "field access requires a record, got %s", type_name(ot));
        return type_unit(S);
    }
    s_record_decl *rec = &S->mod->records[ot->record_index];
    for (s_size i = 0; i < rec->nfields; ++i) {
        if (strcmp(rec->fields[i].name, e->u.field.field) == 0) {
            e->u.field.field_index = rec->fields[i].index;
            e->is_lvalue = true;
            return rec->fields[i].type;
        }
    }
    s_diag_error(S->diag, e->loc, 417,
                 "record '%s' has no field '%s'", rec->name, e->u.field.field);
    return type_unit(S);
}

static s_type *check_record_init(s_sema *S, s_expr *e) {
    int ridx = resolve_record_name(S, e->u.rec.name);
    if (ridx < 0) {
        s_diag_error(S->diag, e->loc, 471, "unknown record type '%s'",
                     e->u.rec.name);
        int best = INT_MAX;
        const char *bestc = NULL;
        for (s_size i = 0; i < S->mod->nrecords; ++i)
            update_suggestion(e->u.rec.name, S->mod->records[i].name,
                              &best, &bestc);
        maybe_note_suggestion(S, e->loc, e->u.rec.name, best, bestc);
        return type_unit(S);
    }
    s_record_decl *rec = &S->mod->records[ridx];
    e->u.rec.record_index = ridx;
    s_size nf = rec->nfields;
    s_expr **by = s_arena_calloc(S->arena, nf ? nf : 1, sizeof(s_expr *),
                                  _Alignof(s_expr *));
    if (!by) { e->u.rec.by_field = NULL; e->u.rec.nfields = nf; return type_unit(S); }
    bool ok = true;
    for (s_size i = 0; i < e->u.rec.nprovided; ++i) {
        const char *nm = e->u.rec.fields[i].name;
        int j = -1;
        for (s_size k = 0; k < nf; ++k) {
            if (strcmp(rec->fields[k].name, nm) == 0) { j = (int)k; break; }
        }
        if (j < 0) {
            s_diag_error(S->diag, e->loc, 472, "record '%s' has no field '%s'",
                         rec->name, nm);
            ok = false;
            continue;
        }
        if (by[j]) {
            s_diag_error(S->diag, e->loc, 473, "duplicate field '%s'", nm);
            ok = false;
            continue;
        }
        s_type *vt = check_expr(S, e->u.rec.fields[i].val);
        if (vt && !type_eq(vt, rec->fields[j].type)) {
            s_diag_error(S->diag, e->loc, 475, "field '%s' has type %s, expected %s",
                         nm, type_name(vt), type_name(rec->fields[j].type));
            ok = false;
        }
        by[j] = e->u.rec.fields[i].val;
    }
    for (s_size j = 0; j < nf; ++j) {
        if (!by[j]) {
            s_diag_error(S->diag, e->loc, 474, "missing field '%s'",
                         rec->fields[j].name);
            ok = false;
        }
    }
    e->u.rec.by_field = by;
    e->u.rec.nfields = nf;
    if (!ok) return type_unit(S);
    s_type *t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
    if (t) {
        t->kind = S_TYPE_RECORD;
        t->record_index = ridx;
        t->record_name = rec->name;
    }
    return t;
}

static s_type *check_assign(s_sema *S, s_expr *e) {
    s_expr *lhs = e->kind == S_EXPR_COMPOUND_ASSIGN ? e->u.compound.lhs
                                                    : e->u.assign.lhs;
    s_expr *rhs = e->kind == S_EXPR_COMPOUND_ASSIGN ? e->u.compound.rhs
                                                    : e->u.assign.rhs;
    s_type *lt = check_expr(S, lhs);
    s_type *rt = check_expr(S, rhs);
    if (!lt || !rt) return type_unit(S);
    if (!lhs->is_lvalue) {
        s_diag_error(S->diag, e->loc, 420, "left side of assignment is not assignable");
        return type_unit(S);
    }
    if (lhs->kind == S_EXPR_VAR && !lhs->u.var.is_mut) {
        s_diag_error(S->diag, e->loc, 422,
                     "cannot assign to immutable variable '%s'",
                     lhs->u.var.name);
    }
    if (!type_eq(lt, rt)) {
        s_diag_error(S->diag, e->loc, 421,
                     "cannot assign %s to %s", type_name(rt), type_name(lt));
    }
    return type_unit(S);
}

static s_type *lambda_type(s_sema *S, const s_func_decl *f) {
    s_type *t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
    if (!t) return NULL;
    t->kind = S_TYPE_FN;
    t->nparams = f->nparams - 1;
    t->ret = f->ret_type;
    t->params = s_arena_calloc(S->arena, t->nparams ? t->nparams : 1,
                                sizeof(s_type *), _Alignof(s_type *));
    if (!t->params) return NULL;
    for (s_size i = 0; i < t->nparams; ++i)
        t->params[i] = f->params[i + 1].type;
    return t;
}

static bool check_block(s_sema *S, s_stmt *blk);

static s_type *check_lambda(s_sema *S, s_expr *e) {
    int fidx = e->u.lambda.func_index;
    if (fidx < 0 || fidx >= (int)S->mod->nfuncs) return type_unit(S);
    s_func_decl *f = &S->mod->funcs[fidx];

    int saved_slot = S->next_slot;
    s_scope sc;
    sc.parent = S->scope;
    sc.syms = NULL;
    sc.count = 0;
    sc.cap = 0;
    sc.depth = (S->scope ? S->scope->depth : 0) + 1;
    e->u.lambda.base_depth = sc.depth;

    for (s_size i = 1; i < f->nparams; ++i) {
        s_sym sym;
        sym.name = f->params[i].name;
        sym.type = f->params[i].type;
        sym.index = f->params[i].index;
        sym.is_mut = false;
        if (!scope_push_sym(&sc, sym)) { scope_pop(&sc); return type_unit(S); }
    }

    s_scope *saved_scope = S->scope;
    s_func_decl *saved_fn = S->fn;
    s_expr *saved_lam = S->lambda_owner;
    S->scope = &sc;
    S->fn = f;
    S->lambda_owner = e;
    S->next_slot = (int)f->nparams;

    check_block(S, f->body);

    S->scope = saved_scope;
    S->fn = saved_fn;
    S->lambda_owner = saved_lam;
    S->next_slot = saved_slot;
    scope_pop(&sc);

    return lambda_type(S, f);
}

static s_type *check_expr(s_sema *S, s_expr *e) {
    if (++S->depth > 4096) {
        s_diag_error(S->diag, e->loc, 430, "expression too deep");
        --S->depth;
        return type_unit(S);
    }
    s_type *t = NULL;
    switch (e->kind) {
    case S_EXPR_INT_LIT:
        t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
        if (t) t->kind = S_TYPE_INT;
        break;
    case S_EXPR_FLOAT_LIT:
        t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
        if (t) t->kind = S_TYPE_FLOAT;
        break;
    case S_EXPR_STR_LIT:
        t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
        if (t) t->kind = S_TYPE_STR;
        break;
    case S_EXPR_BOOL_LIT:
        t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
        if (t) t->kind = S_TYPE_BOOL;
        break;
    case S_EXPR_NONE_LIT: {
        t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
        if (t) t->kind = S_TYPE_UNIT;
        break;
    }
    case S_EXPR_ARRAY_LIT: {
        if (e->u.array.count == 0) {
            s_diag_error(S->diag, e->loc, 432,
                         "empty array literal requires an explicit element type");
            t = type_unit(S);
            break;
        }
        s_type *et = check_expr(S, e->u.array.elems[0]);
        bool ok = et != NULL;
        if (ok) {
            for (s_size i = 1; i < e->u.array.count; ++i) {
                s_type *it = check_expr(S, e->u.array.elems[i]);
                if (!it || !type_eq(et, it)) {
                    s_diag_error(S->diag, e->loc, 433,
                                 "array elements must all have one type, got %s and %s",
                                 type_name(et), it ? type_name(it) : "?");
                    ok = false;
                    break;
                }
            }
        }
        if (!ok) { t = type_unit(S); break; }
        t = s_arena_calloc(S->arena, 1, sizeof(s_type), _Alignof(s_type));
        if (t) { t->kind = S_TYPE_ARRAY; t->elem = et; }
        break;
    }
    case S_EXPR_VAR: {
        int odepth = -1;
        s_sym *sym = scope_lookup(S->scope, e->u.var.name, &odepth);
        if (!sym) {
            for (s_size ci = 0; ci < S->mod->nconsts; ++ci) {
                if (strcmp(S->mod->consts[ci].name, e->u.var.name) == 0) {
                    e->kind = S_EXPR_CONST_REF;
                    e->u.cref.const_index = (int)ci;
                    t = S->mod->consts[ci].type;
                    break;
                }
            }
            if (e->kind == S_EXPR_CONST_REF) break;
            s_func_decl *f = resolve_function(S, e->u.var.name);
            if (f) {
                e->kind = S_EXPR_FUNC_REF;
                e->u.var.var_index = (int)(f - S->mod->funcs);
                t = func_type(S, f);
                break;
            }
            s_diag_error(S->diag, e->loc, 431,
                         "unknown variable '%s'", e->u.var.name);
            t = type_unit(S);
            break;
        }
        if (S->lambda_owner && odepth >= 0 &&
            odepth < S->lambda_owner->u.lambda.base_depth) {
            /* captured from an enclosing function scope; dedup by source slot */
            s_expr *lam = S->lambda_owner;
            int idx = -1;
            for (int k = 0; k < lam->u.lambda.ncaptures; ++k) {
                if (lam->u.lambda.cap_src[k] == sym->index) { idx = k; break; }
            }
            if (idx < 0) {
                idx = lam->u.lambda.ncaptures;
                int newn = idx + 1;
                int *ncap = s_arena_alloc(S->arena, sizeof(int) * (s_size)newn,
                                          _Alignof(int));
                if (!ncap) return type_unit(S);
                if (newn > 1)
                    memcpy(ncap, lam->u.lambda.cap_src, sizeof(int) * (s_size)idx);
                ncap[idx] = sym->index;
                lam->u.lambda.cap_src = ncap;
                lam->u.lambda.ncaptures = newn;
            }
            e->u.var.is_capture = true;
            e->u.var.capture_index = idx;
            e->u.var.is_mut = sym->is_mut;
            t = sym->type;
            e->is_lvalue = true;
            break;
        }
        e->u.var.var_index = sym->index;
        e->u.var.is_mut = sym->is_mut;
        t = sym->type;
        e->is_lvalue = true; /* a variable is assignable if mutable; checked separately */
        break;
    }
    case S_EXPR_CONST_REF: {
        int ci = e->u.cref.const_index;
        if (ci >= 0 && ci < (int)S->mod->nconsts) {
            t = S->mod->consts[ci].type;
        } else {
            t = type_unit(S);
        }
        break;
    }
    case S_EXPR_FUNC_REF: {
        int f = e->u.var.var_index;
        if (f >= 0 && f < (int)S->mod->nfuncs) {
            t = func_type(S, &S->mod->funcs[f]);
        } else {
            t = type_unit(S);
        }
        break;
    }
    case S_EXPR_CALL: t = check_call(S, e); break;
    case S_EXPR_RECORD_INIT: t = check_record_init(S, e); break;
    case S_EXPR_LAMBDA: t = check_lambda(S, e); break;
    case S_EXPR_INDEX: t = check_index(S, e); break;
    case S_EXPR_FIELD: t = check_field(S, e); break;
    case S_EXPR_UNARY: t = check_unary(S, e); break;
    case S_EXPR_BINARY: t = check_binary(S, e); break;
    case S_EXPR_COND: {
        s_type *ct = check_expr(S, e->u.cond.cond);
        if (ct && ct->kind != S_TYPE_BOOL) {
            s_diag_error(S->diag, e->loc, 402,
                         "condition must be bool, got %s", type_name(ct));
        }
        s_type *tt = check_expr(S, e->u.cond.when_true);
        s_type *ft = check_expr(S, e->u.cond.when_false);
        if (tt && ft && type_eq(tt, ft)) {
            t = tt;
        } else {
            s_diag_error(S->diag, e->loc, 402,
                         "conditional branches must have the same type, got %s and %s",
                         tt ? type_name(tt) : "?", ft ? type_name(ft) : "?");
            t = type_unit(S);
        }
        break;
    }
    case S_EXPR_ASSIGN: t = check_assign(S, e); break;
    case S_EXPR_COMPOUND_ASSIGN: t = check_assign(S, e); break;
    }
    e->type = t;
    --S->depth;
    return t;
}

/* ------------------------------------------------------------------ */
/* Statement checking                                                  */
/* ------------------------------------------------------------------ */

static bool check_stmt(s_sema *S, s_stmt *s);

static bool push_let(s_sema *S, s_stmt *s, s_type *t, bool is_mut,
                     int *out_index) {
    s_sym sym;
    sym.name = s->u.let.name;
    sym.type = t;
    sym.index = S->next_slot++;
    sym.is_mut = is_mut;
    if (!scope_push_sym(S->scope, sym)) {
        s_diag_error(S->diag, s->loc, 440, "out of memory");
        return false;
    }
    s->u.let.var_index = sym.index;
    if (out_index) *out_index = sym.index;
    return true;
}

static bool check_block(s_sema *S, s_stmt *blk) {
    s_scope sc;
    sc.parent = S->scope;
    sc.syms = NULL;
    sc.count = 0;
    sc.cap = 0;
    sc.depth = S->scope ? S->scope->depth + 1 : 0;
    s_scope *saved = S->scope;
    S->scope = &sc;
    bool ok = true;
    for (s_size i = 0; i < blk->u.block.count; ++i) {
        if (!check_stmt(S, blk->u.block.items[i])) ok = false;
    }
    S->scope = saved;
    scope_pop(&sc);
    return ok;
}

static bool check_stmt(s_sema *S, s_stmt *s) {
    switch (s->kind) {
    case S_STMT_LET: {
        s_type *it = NULL;
        s_expr *init = s->u.let.init;
        if (init->kind == S_EXPR_ARRAY_LIT && init->u.array.count == 0) {
            if (s->u.let.decl_type && s->u.let.decl_type->kind == S_TYPE_ARRAY) {
                it = s->u.let.decl_type;
                init->type = it;
            } else {
                s_diag_error(S->diag, init->loc, 432,
                             "empty array literal requires a type ascription, e.g. [int]");
                return false;
            }
        } else {
            it = check_expr(S, init);
        }
        if (!it) return false;
        if (s->u.let.decl_type) {
            if (!type_eq(s->u.let.decl_type, it)) {
                s_diag_error(S->diag, s->loc, 450,
                             "initializer has type %s, expected %s",
                             type_name(it), type_name(s->u.let.decl_type));
            }
            it = s->u.let.decl_type;
        }
        return push_let(S, s, it, s->u.let.is_mut, NULL);
    }
    case S_STMT_EXPR:
        return check_expr(S, s->u.expr.expr) != NULL;
    case S_STMT_RETURN: {
        if (!S->fn) {
            s_diag_error(S->diag, s->loc, 451, "'return' outside function");
            return false;
        }
        s_type *ret = S->fn->ret_type;
        if (s->u.ret.has_expr) {
            s_type *it = check_expr(S, s->u.ret.expr);
            if (it && !type_eq(it, ret)) {
                s_diag_error(S->diag, s->loc, 452,
                             "return type is %s, got %s",
                             type_name(ret), type_name(it));
            }
        } else {
            if (ret && ret->kind != S_TYPE_UNIT) {
                s_diag_error(S->diag, s->loc, 453,
                             "function returns %s, missing return value",
                             type_name(ret));
            }
        }
        return true;
    }
    case S_STMT_IF: {
        s_type *ct = check_expr(S, s->u.ifstmt.cond);
        if (ct && ct->kind != S_TYPE_BOOL) {
            s_diag_error(S->diag, s->u.ifstmt.cond->loc, 454,
                         "if condition must be bool, got %s", type_name(ct));
        }
        bool ok = check_block(S, s->u.ifstmt.then_block);
        if (s->u.ifstmt.else_block) {
            if (s->u.ifstmt.else_block->kind == S_STMT_IF) {
                ok = check_stmt(S, s->u.ifstmt.else_block) && ok;
            } else {
                ok = check_block(S, s->u.ifstmt.else_block) && ok;
            }
        }
        return ok;
    }
    case S_STMT_WHILE: {
        s_type *ct = check_expr(S, s->u.whilestmt.cond);
        if (ct && ct->kind != S_TYPE_BOOL) {
            s_diag_error(S->diag, s->u.whilestmt.cond->loc, 455,
                         "while condition must be bool, got %s", type_name(ct));
        }
        return check_block(S, s->u.whilestmt.body);
    }
    case S_STMT_DO: {
        bool ok = check_block(S, s->u.dostmt.body);
        s_type *ct = check_expr(S, s->u.dostmt.cond);
        if (ct && ct->kind != S_TYPE_BOOL) {
            s_diag_error(S->diag, s->u.dostmt.cond->loc, 455,
                         "do-while condition must be bool, got %s", type_name(ct));
        }
        return ok;
    }
    case S_STMT_FOR: {
        s_type *it = check_expr(S, s->u.forstmt.iter);
        if (!it) return false;
        s_type *elem = NULL;
        if (it->kind == S_TYPE_ARRAY) {
            elem = it->elem;
        } else if (it->kind == S_TYPE_STR) {
            elem = it;
        } else {
            s_diag_error(S->diag, s->u.forstmt.iter->loc, 456,
                         "for-in requires an array or string, got %s", type_name(it));
            return false;
        }
        s_sym sym;
        sym.name = s->u.forstmt.name;
        sym.type = elem;
        sym.index = S->next_slot++;
        sym.is_mut = true;
        if (!scope_push_sym(S->scope, sym)) return false;
        s->u.forstmt.var_index = sym.index;
        bool ok = check_block(S, s->u.forstmt.body);
        /* remove the loop variable scope */
        /* The block pushed its own scope; we push the loop var into the current
           scope before the block, so pop it after. Implement via parent scope. */
        return ok;
    }
    case S_STMT_BREAK:
    case S_STMT_CONTINUE:
        return true;
    case S_STMT_BLOCK:
        return check_block(S, s);
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Compile-time constant evaluation                                    */
/* ------------------------------------------------------------------ */

static bool eval_expr_const(s_sema *S, s_expr *e, s_const_decl *out) {
    switch (e->kind) {
    case S_EXPR_INT_LIT:
        out->lit_kind = S_EXPR_INT_LIT; out->v.i = e->u.i; return true;
    case S_EXPR_FLOAT_LIT:
        out->lit_kind = S_EXPR_FLOAT_LIT; out->v.f = e->u.f; return true;
    case S_EXPR_STR_LIT:
        out->lit_kind = S_EXPR_STR_LIT;
        out->v.str.text = e->u.str.text;
        out->v.str.len = e->u.str.len;
        return true;
    case S_EXPR_BOOL_LIT:
        out->lit_kind = S_EXPR_BOOL_LIT; out->v.b = e->u.b; return true;
    case S_EXPR_VAR: {
        for (s_size i = 0; i < S->mod->nconsts; ++i) {
            s_const_decl *c = &S->mod->consts[i];
            if (c->resolved && strcmp(c->name, e->u.var.name) == 0) {
                out->lit_kind = c->lit_kind;
                out->v = c->v;
                return true;
            }
        }
        s_diag_error(S->diag, e->loc, 480,
                     "unknown or forward-referenced constant '%s'",
                     e->u.var.name);
        return false;
    }
    case S_EXPR_UNARY: {
        s_const_decl a;
        if (!eval_expr_const(S, e->u.unary.operand, &a)) return false;
        if (e->u.unary.op == S_UN_NEG) {
            if (a.lit_kind == S_EXPR_INT_LIT) {
                out->lit_kind = S_EXPR_INT_LIT; out->v.i = -a.v.i; return true;
            }
            if (a.lit_kind == S_EXPR_FLOAT_LIT) {
                out->lit_kind = S_EXPR_FLOAT_LIT; out->v.f = -a.v.f; return true;
            }
        } else if (e->u.unary.op == S_UN_NOT) {
            if (a.lit_kind == S_EXPR_BOOL_LIT) {
                out->lit_kind = S_EXPR_BOOL_LIT; out->v.b = !a.v.b; return true;
            }
        } else if (e->u.unary.op == S_UN_BITNOT) {
            if (a.lit_kind == S_EXPR_INT_LIT) {
                out->lit_kind = S_EXPR_INT_LIT; out->v.i = ~a.v.i; return true;
            }
        }
        s_diag_error(S->diag, e->loc, 481,
                     "operator not supported in a constant expression");
        return false;
    }
    case S_EXPR_BINARY: {
        s_const_decl a, b;
        if (!eval_expr_const(S, e->u.binary.lhs, &a)) return false;
        if (!eval_expr_const(S, e->u.binary.rhs, &b)) return false;
        s_bin_op op = e->u.binary.op;
        if (op == S_BIN_ADD && a.lit_kind == S_EXPR_STR_LIT &&
            b.lit_kind == S_EXPR_STR_LIT) {
            s_size t = a.v.str.len + b.v.str.len;
            char *n = s_arena_alloc(S->arena, t + 1, 1);
            if (!n) return false;
            memcpy(n, a.v.str.text, a.v.str.len);
            memcpy(n + a.v.str.len, b.v.str.text, b.v.str.len);
            out->lit_kind = S_EXPR_STR_LIT;
            out->v.str.text = n;
            out->v.str.len = t;
            return true;
        }
        if (a.lit_kind == S_EXPR_INT_LIT && b.lit_kind == S_EXPR_INT_LIT) {
            s_i64 r;
            switch (op) {
            case S_BIN_ADD: r = a.v.i + b.v.i; break;
            case S_BIN_SUB: r = a.v.i - b.v.i; break;
            case S_BIN_MUL: r = a.v.i * b.v.i; break;
            case S_BIN_DIV:
                if (b.v.i == 0) { s_diag_error(S->diag, e->loc, 482, "division by zero in constant"); return false; }
                r = a.v.i / b.v.i; break;
            case S_BIN_MOD:
                if (b.v.i == 0) { s_diag_error(S->diag, e->loc, 482, "division by zero in constant"); return false; }
                r = a.v.i % b.v.i; break;
            case S_BIN_BIT_AND: r = a.v.i & b.v.i; break;
            case S_BIN_BIT_OR: r = a.v.i | b.v.i; break;
            case S_BIN_BIT_XOR: r = a.v.i ^ b.v.i; break;
            case S_BIN_SHL: r = (b.v.i >= 64) ? 0 : (s_i64)((s_u64)a.v.i << b.v.i); break;
            case S_BIN_SHR: r = (b.v.i >= 64) ? 0 : (s_i64)((s_u64)a.v.i >> b.v.i); break;
            default:
                s_diag_error(S->diag, e->loc, 481, "operator not supported in a constant expression");
                return false;
            }
            out->lit_kind = S_EXPR_INT_LIT; out->v.i = r; return true;
        }
        if (a.lit_kind == S_EXPR_FLOAT_LIT && b.lit_kind == S_EXPR_FLOAT_LIT) {
            double r;
            switch (op) {
            case S_BIN_ADD: r = a.v.f + b.v.f; break;
            case S_BIN_SUB: r = a.v.f - b.v.f; break;
            case S_BIN_MUL: r = a.v.f * b.v.f; break;
            case S_BIN_DIV: r = a.v.f / b.v.f; break;
            default:
                s_diag_error(S->diag, e->loc, 481, "operator not supported in a constant expression");
                return false;
            }
            out->lit_kind = S_EXPR_FLOAT_LIT; out->v.f = r; return true;
        }
        if ((a.lit_kind == S_EXPR_INT_LIT && b.lit_kind == S_EXPR_FLOAT_LIT) ||
            (a.lit_kind == S_EXPR_FLOAT_LIT && b.lit_kind == S_EXPR_INT_LIT)) {
            double x = (a.lit_kind == S_EXPR_FLOAT_LIT) ? a.v.f : (double)a.v.i;
            double y = (b.lit_kind == S_EXPR_FLOAT_LIT) ? b.v.f : (double)b.v.i;
            double r;
            switch (op) {
            case S_BIN_ADD: r = x + y; break;
            case S_BIN_SUB: r = x - y; break;
            case S_BIN_MUL: r = x * y; break;
            case S_BIN_DIV: r = x / y; break;
            default:
                s_diag_error(S->diag, e->loc, 481, "operator not supported in a constant expression");
                return false;
            }
            out->lit_kind = S_EXPR_FLOAT_LIT; out->v.f = r; return true;
        }
        if (op == S_BIN_AND && a.lit_kind == S_EXPR_BOOL_LIT &&
            b.lit_kind == S_EXPR_BOOL_LIT) {
            out->lit_kind = S_EXPR_BOOL_LIT; out->v.b = a.v.b && b.v.b; return true;
        }
        if (op == S_BIN_OR && a.lit_kind == S_EXPR_BOOL_LIT &&
            b.lit_kind == S_EXPR_BOOL_LIT) {
            out->lit_kind = S_EXPR_BOOL_LIT; out->v.b = a.v.b || b.v.b; return true;
        }
        s_diag_error(S->diag, e->loc, 481,
                     "operands are not valid in a constant expression");
        return false;
    }
    default:
        s_diag_error(S->diag, e->loc, 481,
                     "expression is not a compile-time constant");
        return false;
    }
}

static bool eval_const(s_sema *S, s_const_decl *c) {
    s_const_decl tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (!eval_expr_const(S, c->init, &tmp)) return false;
    c->lit_kind = tmp.lit_kind;
    c->v = tmp.v;
    c->resolved = true;
    return true;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

bool s_sema_run(s_module *mod, s_arena *arena, s_diag_ctx *diag) {
    s_sema S;
    memset(&S, 0, sizeof(S));
    S.mod = mod;
    S.arena = arena;
    S.diag = diag;

    /* resolve record types in all type annotations */
    for (s_size r = 0; r < mod->nrecords; ++r) {
        s_record_decl *rec = &mod->records[r];
        for (s_size i = 0; i < rec->nfields; ++i) {
            s_type *ft = rec->fields[i].type;
            if (ft->kind == S_TYPE_RECORD) {
                int idx = resolve_record(&S, ft, rec->fields[i].loc);
                if (idx >= 0) ft->record_index = idx;
            }
        }
    }

    bool ok = true;

    /* detect duplicate top-level declarations (surfaces across `use` merges) */
    for (s_size i = 0; i < mod->nfuncs; ++i) {
        for (s_size j = i + 1; j < mod->nfuncs; ++j) {
            if (strcmp(mod->funcs[i].name, mod->funcs[j].name) == 0) {
                s_diag_error(diag, mod->funcs[j].loc, 477,
                             "duplicate function '%s'", mod->funcs[j].name);
                ok = false;
            }
        }
    }
    for (s_size i = 0; i < mod->nrecords; ++i) {
        for (s_size j = i + 1; j < mod->nrecords; ++j) {
            if (strcmp(mod->records[i].name, mod->records[j].name) == 0) {
                s_diag_error(diag, mod->records[j].loc, 478,
                             "duplicate type '%s'", mod->records[j].name);
                ok = false;
            }
        }
    }
    for (s_size i = 0; i < mod->nconsts; ++i) {
        for (s_size j = i + 1; j < mod->nconsts; ++j) {
            if (strcmp(mod->consts[i].name, mod->consts[j].name) == 0) {
                s_diag_error(diag, mod->consts[j].loc, 479,
                             "duplicate constant '%s'", mod->consts[j].name);
                ok = false;
            }
        }
    }

    /* resolve constants: evaluate, then infer or validate their types */
    for (s_size ci = 0; ci < mod->nconsts; ++ci) {
        s_const_decl *c = &mod->consts[ci];
        if (!c->resolved && !eval_const(&S, c)) { ok = false; continue; }
        s_type_kind k;
        switch (c->lit_kind) {
        case S_EXPR_INT_LIT: k = S_TYPE_INT; break;
        case S_EXPR_FLOAT_LIT: k = S_TYPE_FLOAT; break;
        case S_EXPR_STR_LIT: k = S_TYPE_STR; break;
        case S_EXPR_BOOL_LIT: k = S_TYPE_BOOL; break;
        default: k = S_TYPE_VOID; break;
        }
        if (!c->type) {
            c->type = s_arena_calloc(arena, 1, sizeof(s_type), _Alignof(s_type));
            if (!c->type) return false;
            c->type->kind = k;
            c->type->loc = c->loc;
        } else if (c->type->kind != k) {
            s_diag_error(diag, c->loc, 470,
                         "constant '%s' has type %s, but its value is %s",
                         c->name, type_name(c->type), type_kind_name(k));
            ok = false;
        }
    }

    for (s_size f = 0; f < mod->nfuncs; ++f) {
        s_func_decl *fn = &mod->funcs[f];
        if (fn->is_lambda) continue; /* analyzed within its enclosing function */
        S.fn = fn;
        S.next_slot = 0;

        /* parameter scope */
        s_scope sc;
        sc.parent = NULL;
        sc.syms = NULL;
        sc.count = 0;
        sc.cap = 0;
        sc.depth = 0;
        S.scope = &sc;

        for (s_size i = 0; i < fn->nparams; ++i) {
            s_type *pt = fn->params[i].type;
            if (pt->kind == S_TYPE_RECORD) {
                int idx = resolve_record(&S, pt, fn->params[i].type->loc);
                if (idx >= 0) pt->record_index = idx;
            }
            s_sym sym;
            sym.name = fn->params[i].name;
            sym.type = pt;
            sym.index = (int)i;
            sym.is_mut = false;
            if (!scope_push_sym(&sc, sym)) return false;
            fn->params[i].index = (int)i;
            S.next_slot++;
        }
        if (fn->ret_type && fn->ret_type->kind == S_TYPE_RECORD) {
            resolve_record(&S, fn->ret_type, fn->loc);
        }
        if (!check_block(&S, fn->body)) ok = false;
        scope_pop(&sc);
    }

    S.fn = NULL;

    /* main must exist */
    mod->main_index = -1;
    for (s_size f = 0; f < mod->nfuncs; ++f) {
        if (strcmp(mod->funcs[f].name, "main") == 0) {
            mod->main_index = (int)f;
            s_type *rt = mod->funcs[f].ret_type;
            if (rt && rt->kind != S_TYPE_INT && rt->kind != S_TYPE_UNIT) {
                s_diag_error(diag, mod->funcs[f].loc, 460,
                             "main must return int or unit, got %s", type_name(rt));
                ok = false;
            }
            break;
        }
    }
    if (mod->main_index < 0) {
        s_diag_error(diag, mod->loc, 461, "program has no 'main' function");
        ok = false;
    }

    return ok && !s_diag_has_errors(diag);
}
