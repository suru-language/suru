#include "suru/parser.h"

#include <string.h>

#define SU_PARSE_MAX_DEPTH 512
#define SU_PARSE_MAX_ITEMS 1u << 20

typedef struct s_param_entry {
    const char *name;
    s_type *type;
} s_param_entry;

/* ------------------------------------------------------------------ */
/* Dynamic arrays over the arena (leaks old buffers; bulk-freed)       */
/* ------------------------------------------------------------------ */

static bool vec_reserve(s_arena *a, void **arr, s_size *cap,
                        s_size want, s_size esize) {
    if (want <= *cap) return true;
    s_size ncap = *cap ? *cap : 8;
    while (ncap < want) {
        s_size next;
        if (!s_size_mul_ovf(ncap, 2, &next)) return false;
        ncap = next;
    }
    s_size bytes;
    if (!s_size_mul_ovf(ncap, esize, &bytes)) return false;
    void *narr = s_arena_alloc(a, bytes, _Alignof(max_align_t));
    if (!narr) return false;
    if (*arr && *cap > 0) {
        memcpy(narr, *arr, (*cap) * esize);
    }
    *arr = narr;
    *cap = ncap;
    return true;
}

#define VEC_PUSH(a, arr, count, cap, esize, elem) do {                   \
    if (!vec_reserve((a), (void **)&(arr), &(cap), (count) + 1, (esize))) \
        return false;                                                     \
    (arr)[(count)++] = (elem);                                            \
} while (0)

#define VEC_GROW(a, arr, count, cap, esize, n) do {                     \
    if (!vec_reserve((a), (void **)&(arr), &(cap), (count) + (n), (esize))) \
        return false;                                                     \
    (count) += (n);                                                       \
} while (0)

/* ------------------------------------------------------------------ */
/* Parser setup                                                        */
/* ------------------------------------------------------------------ */

static void fetch(s_parser *p, s_token *out) {
    if (p->has_ahead) {
        *out = p->ahead;
        p->has_ahead = false;
        return;
    }
    if (!s_lexer_next(&p->lx, out)) {
        out->kind = S_TOK_EOF;
    }
}

static void advance(s_parser *p) {
    fetch(p, &p->cur);
}

static bool at(s_parser *p, s_tok_kind k) { return p->cur.kind == k; }

static bool at_ident_word(s_parser *p, const char *w) {
    s_size len = strlen(w);
    return at(p, S_TOK_IDENT) && p->cur.v.s.len == len &&
           memcmp(p->cur.v.s.text, w, len) == 0;
}

static bool accept(s_parser *p, s_tok_kind k) {
    if (at(p, k)) { advance(p); return true; }
    return false;
}

static bool expect(s_parser *p, s_tok_kind k, int code) {
    if (at(p, k)) { advance(p); return true; }
    s_diag_error(p->diag, p->cur.loc, code, "expected %s, found %s",
                 s_tok_kind_name(k), s_tok_kind_name(p->cur.kind));
    return false;
}

static bool enter_depth(s_parser *p, int code) {
    if (p->depth >= SU_PARSE_MAX_DEPTH) {
        s_diag_error(p->diag, p->cur.loc, code,
                     "nesting too deep (limit %d)", SU_PARSE_MAX_DEPTH);
        return false;
    }
    p->depth++;
    return true;
}
static void leave_depth(s_parser *p) { p->depth--; }

static const char *ident_text(s_parser *p) {
    return s_arena_strndup(p->arena, p->cur.v.s.text, p->cur.v.s.len);
}

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

static s_type *parse_type(s_parser *p);

static s_type *type_prim(s_parser *p, s_type_kind k, s_source_loc loc) {
    s_type *t = s_arena_calloc(p->arena, 1, sizeof(s_type), _Alignof(s_type));
    if (!t) return NULL;
    t->kind = k;
    t->loc = loc;
    return t;
}

static s_type *parse_type(s_parser *p) {
    s_source_loc loc = p->cur.loc;
    if (at_ident_word(p, "int")) { advance(p); return type_prim(p, S_TYPE_INT, loc); }
    if (at_ident_word(p, "float")) { advance(p); return type_prim(p, S_TYPE_FLOAT, loc); }
    if (at_ident_word(p, "bool")) { advance(p); return type_prim(p, S_TYPE_BOOL, loc); }
    if (at_ident_word(p, "str")) { advance(p); return type_prim(p, S_TYPE_STR, loc); }
    if (at_ident_word(p, "unit")) { advance(p); return type_prim(p, S_TYPE_UNIT, loc); }
    if (accept(p, S_TOK_LBRACKET)) {
        s_type *elem = parse_type(p);
        if (!elem) return NULL;
        if (!expect(p, S_TOK_RBRACKET, 300)) return NULL;
        s_type *t = type_prim(p, S_TYPE_ARRAY, loc);
        if (!t) return NULL;
        t->elem = elem;
        return t;
    }
    if (accept(p, S_TOK_KW_FUNC)) {
        s_type *t = type_prim(p, S_TYPE_FN, loc);
        if (!t) return NULL;
        if (!expect(p, S_TOK_LPAREN, 301)) return NULL;
        s_type **params = NULL;
        s_size n = 0, cap = 0;
        if (!at(p, S_TOK_RPAREN)) {
            for (;;) {
                s_type *pt = parse_type(p);
                if (!pt) return NULL;
                VEC_PUSH(p->arena, params, n, cap, sizeof(s_type *), pt);
                if (!accept(p, S_TOK_COMMA)) break;
            }
        }
        expect(p, S_TOK_RPAREN, 302);
        t->params = params;
        t->nparams = n;
        if (accept(p, S_TOK_ARROW)) {
            t->ret = parse_type(p);
            if (!t->ret) return NULL;
        } else {
            t->ret = type_prim(p, S_TYPE_UNIT, loc);
        }
        return t;
    }
    if (at(p, S_TOK_IDENT)) {
        s_type *t = type_prim(p, S_TYPE_RECORD, loc);
        if (!t) return NULL;
        t->record_name = ident_text(p);
        advance(p);
        t->record_index = -1;
        return t;
    }
    s_diag_error(p->diag, loc, 303, "expected a type, found %s",
                 s_tok_kind_name(p->cur.kind));
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

static s_expr *expr_alloc(s_parser *p, s_expr_kind k, s_source_loc loc) {
    s_expr *e = s_arena_calloc(p->arena, 1, sizeof(s_expr), _Alignof(s_expr));
    if (!e) return NULL;
    e->kind = k;
    e->loc = loc;
    return e;
}

static s_expr *parse_assignment(s_parser *p);
static s_expr *parse_primary(s_parser *p);

static s_expr *parse_postfix(s_parser *p) {
    s_expr *e = parse_primary(p);
    if (!e) return NULL;
    for (;;) {
        if (accept(p, S_TOK_LPAREN)) {
            s_expr *call = expr_alloc(p, S_EXPR_CALL, e->loc);
            if (!call) return NULL;
            s_expr **args = NULL;
            s_size n = 0, cap = 0;
            if (!at(p, S_TOK_RPAREN)) {
                for (;;) {
                    s_expr *a = parse_assignment(p);
                    if (!a) return NULL;
                    VEC_PUSH(p->arena, args, n, cap, sizeof(s_expr *), a);
                    if (!accept(p, S_TOK_COMMA)) break;
                }
            }
            if (!expect(p, S_TOK_RPAREN, 304)) return NULL;
            call->u.call.callee = e;
            call->u.call.args = args;
            call->u.call.nargs = n;
            e = call;
        } else if (accept(p, S_TOK_LBRACKET)) {
            s_expr *idx = expr_alloc(p, S_EXPR_INDEX, e->loc);
            if (!idx) return NULL;
            s_expr *ie = parse_assignment(p);
            if (!ie) return NULL;
            if (!expect(p, S_TOK_RBRACKET, 305)) return NULL;
            idx->u.index.obj = e;
            idx->u.index.index = ie;
            e = idx;
        } else if (accept(p, S_TOK_DOT)) {
            if (!at(p, S_TOK_IDENT)) {
                s_diag_error(p->diag, p->cur.loc, 306,
                             "expected field name after '.'");
                return NULL;
            }
            s_expr *f = expr_alloc(p, S_EXPR_FIELD, e->loc);
            if (!f) return NULL;
            f->u.field.obj = e;
            f->u.field.field = ident_text(p);
            f->u.field.field_index = -1;
            advance(p);
            e = f;
        } else {
            break;
        }
    }
    return e;
}

static s_expr *parse_unary(s_parser *p) {
    if (at(p, S_TOK_MINUS) || at(p, S_TOK_NOT)) {
        s_source_loc loc = p->cur.loc;
        s_un_op op = at(p, S_TOK_MINUS) ? S_UN_NEG : S_UN_NOT;
        advance(p);
        s_expr *operand = parse_unary(p);
        if (!operand) return NULL;
        s_expr *e = expr_alloc(p, S_EXPR_UNARY, loc);
        if (!e) return NULL;
        e->u.unary.op = op;
        e->u.unary.operand = operand;
        return e;
    }
    return parse_postfix(p);
}

static s_expr *parse_multiplicative(s_parser *p) {
    s_expr *lhs = parse_unary(p);
    if (!lhs) return NULL;
    for (;;) {
        s_bin_op op;
        if (accept(p, S_TOK_STAR)) op = S_BIN_MUL;
        else if (accept(p, S_TOK_SLASH)) op = S_BIN_DIV;
        else if (accept(p, S_TOK_PERCENT)) op = S_BIN_MOD;
        else break;
        s_expr *rhs = parse_unary(p);
        if (!rhs) return NULL;
        s_expr *e = expr_alloc(p, S_EXPR_BINARY, lhs->loc);
        if (!e) return NULL;
        e->u.binary.op = op;
        e->u.binary.lhs = lhs;
        e->u.binary.rhs = rhs;
        lhs = e;
    }
    return lhs;
}

static s_expr *parse_additive(s_parser *p) {
    s_expr *lhs = parse_multiplicative(p);
    if (!lhs) return NULL;
    for (;;) {
        s_bin_op op;
        if (accept(p, S_TOK_PLUS)) op = S_BIN_ADD;
        else if (accept(p, S_TOK_MINUS)) op = S_BIN_SUB;
        else break;
        s_expr *rhs = parse_multiplicative(p);
        if (!rhs) return NULL;
        s_expr *e = expr_alloc(p, S_EXPR_BINARY, lhs->loc);
        if (!e) return NULL;
        e->u.binary.op = op;
        e->u.binary.lhs = lhs;
        e->u.binary.rhs = rhs;
        lhs = e;
    }
    return lhs;
}

static s_expr *parse_comparison(s_parser *p) {
    s_expr *lhs = parse_additive(p);
    if (!lhs) return NULL;
    s_bin_op op;
    if (accept(p, S_TOK_EQ)) op = S_BIN_EQ;
    else if (accept(p, S_TOK_NE)) op = S_BIN_NE;
    else if (accept(p, S_TOK_LT)) op = S_BIN_LT;
    else if (accept(p, S_TOK_LE)) op = S_BIN_LE;
    else if (accept(p, S_TOK_GT)) op = S_BIN_GT;
    else if (accept(p, S_TOK_GE)) op = S_BIN_GE;
    else return lhs;
    s_expr *rhs = parse_additive(p);
    if (!rhs) return NULL;
    s_expr *e = expr_alloc(p, S_EXPR_BINARY, lhs->loc);
    if (!e) return NULL;
    e->u.binary.op = op;
    e->u.binary.lhs = lhs;
    e->u.binary.rhs = rhs;
    return e;
}

static s_expr *parse_logical_and(s_parser *p) {
    s_expr *lhs = parse_comparison(p);
    if (!lhs) return NULL;
    while (accept(p, S_TOK_AND)) {
        s_expr *rhs = parse_comparison(p);
        if (!rhs) return NULL;
        s_expr *e = expr_alloc(p, S_EXPR_BINARY, lhs->loc);
        if (!e) return NULL;
        e->u.binary.op = S_BIN_AND;
        e->u.binary.lhs = lhs;
        e->u.binary.rhs = rhs;
        lhs = e;
    }
    return lhs;
}

static s_expr *parse_logical_or(s_parser *p) {
    s_expr *lhs = parse_logical_and(p);
    if (!lhs) return NULL;
    while (accept(p, S_TOK_OR)) {
        s_expr *rhs = parse_logical_and(p);
        if (!rhs) return NULL;
        s_expr *e = expr_alloc(p, S_EXPR_BINARY, lhs->loc);
        if (!e) return NULL;
        e->u.binary.op = S_BIN_OR;
        e->u.binary.lhs = lhs;
        e->u.binary.rhs = rhs;
        lhs = e;
    }
    return lhs;
}

static s_expr *parse_assignment(s_parser *p) {
    s_expr *lhs = parse_logical_or(p);
    if (!lhs) return NULL;
    s_tok_kind k = p->cur.kind;
    bool is_compound = false;
    s_bin_op cop = S_BIN_ADD;
    if (k == S_TOK_ASSIGN) {
        advance(p);
    } else if (k == S_TOK_PLUS_ASSIGN) { advance(p); is_compound = true; cop = S_BIN_ADD; }
    else if (k == S_TOK_MINUS_ASSIGN) { advance(p); is_compound = true; cop = S_BIN_SUB; }
    else if (k == S_TOK_STAR_ASSIGN) { advance(p); is_compound = true; cop = S_BIN_MUL; }
    else if (k == S_TOK_SLASH_ASSIGN) { advance(p); is_compound = true; cop = S_BIN_DIV; }
    else return lhs;

    s_expr *rhs = parse_assignment(p);
    if (!rhs) return NULL;
    s_expr *e = expr_alloc(p, is_compound ? S_EXPR_COMPOUND_ASSIGN : S_EXPR_ASSIGN,
                           lhs->loc);
    if (!e) return NULL;
    if (is_compound) {
        e->u.compound.op = cop;
        e->u.compound.lhs = lhs;
        e->u.compound.rhs = rhs;
    } else {
        e->u.assign.lhs = lhs;
        e->u.assign.rhs = rhs;
    }
    return e;
}

static s_expr *parse_primary(s_parser *p) {
    s_source_loc loc = p->cur.loc;
    if (at(p, S_TOK_INT)) {
        s_expr *e = expr_alloc(p, S_EXPR_INT_LIT, loc);
        if (!e) return NULL;
        e->u.i = p->cur.v.i;
        advance(p);
        return e;
    }
    if (at(p, S_TOK_FLOAT)) {
        s_expr *e = expr_alloc(p, S_EXPR_FLOAT_LIT, loc);
        if (!e) return NULL;
        e->u.f = p->cur.v.f;
        advance(p);
        return e;
    }
    if (at(p, S_TOK_STR)) {
        s_expr *e = expr_alloc(p, S_EXPR_STR_LIT, loc);
        if (!e) return NULL;
        e->u.str.text = s_arena_strdup(p->arena, p->cur.v.s.text);
        e->u.str.len = p->cur.v.s.len;
        advance(p);
        return e;
    }
    if (at(p, S_TOK_TRUE)) {
        s_expr *e = expr_alloc(p, S_EXPR_BOOL_LIT, loc);
        if (!e) return NULL;
        e->u.b = true;
        advance(p);
        return e;
    }
    if (at(p, S_TOK_FALSE)) {
        s_expr *e = expr_alloc(p, S_EXPR_BOOL_LIT, loc);
        if (!e) return NULL;
        e->u.b = false;
        advance(p);
        return e;
    }
    if (at(p, S_TOK_NONE)) {
        s_expr *e = expr_alloc(p, S_EXPR_NONE_LIT, loc);
        if (!e) return NULL;
        advance(p);
        return e;
    }
    if (at(p, S_TOK_IDENT)) {
        s_expr *e = expr_alloc(p, S_EXPR_VAR, loc);
        if (!e) return NULL;
        e->u.var.name = ident_text(p);
        e->u.var.var_index = -1;
        advance(p);
        return e;
    }
    if (accept(p, S_TOK_LPAREN)) {
        s_expr *e = parse_assignment(p);
        if (!e) return NULL;
        if (!expect(p, S_TOK_RPAREN, 308)) return NULL;
        return e;
    }
    s_diag_error(p->diag, loc, 307, "unexpected %s in expression",
                 s_tok_kind_name(p->cur.kind));
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

static s_stmt *stmt_alloc(s_parser *p, s_stmt_kind k, s_source_loc loc) {
    s_stmt *s = s_arena_calloc(p->arena, 1, sizeof(s_stmt), _Alignof(s_stmt));
    if (!s) return NULL;
    s->kind = k;
    s->loc = loc;
    return s;
}

static s_stmt *parse_block(s_parser *p);

static s_stmt *parse_statement(s_parser *p) {
    s_source_loc loc = p->cur.loc;

    if (at(p, S_TOK_KW_LET) || at(p, S_TOK_KW_MUT)) {
        bool is_mut = at(p, S_TOK_KW_MUT);
        advance(p);
        if (!at(p, S_TOK_IDENT)) {
            s_diag_error(p->diag, p->cur.loc, 310,
                         "expected identifier after 'let'/'mut'");
            return NULL;
        }
        s_stmt *s = stmt_alloc(p, S_STMT_LET, loc);
        if (!s) return NULL;
        s->u.let.is_mut = is_mut;
        s->u.let.name = ident_text(p);
        s->u.let.var_index = -1;
        advance(p);
        if (accept(p, S_TOK_COLON)) {
            s->u.let.decl_type = parse_type(p);
            if (!s->u.let.decl_type) return NULL;
        }
        if (!expect(p, S_TOK_ASSIGN, 311)) return NULL;
        s->u.let.init = parse_assignment(p);
        if (!s->u.let.init) return NULL;
        if (!expect(p, S_TOK_SEMI, 312)) return NULL;
        return s;
    }

    if (at(p, S_TOK_KW_RETURN)) {
        advance(p);
        s_stmt *s = stmt_alloc(p, S_STMT_RETURN, loc);
        if (!s) return NULL;
        if (!at(p, S_TOK_SEMI)) {
            s->u.ret.expr = parse_assignment(p);
            if (!s->u.ret.expr) return NULL;
            s->u.ret.has_expr = true;
        }
        if (!expect(p, S_TOK_SEMI, 313)) return NULL;
        return s;
    }

    if (at(p, S_TOK_KW_BREAK)) {
        advance(p);
        s_stmt *s = stmt_alloc(p, S_STMT_BREAK, loc);
        if (!s) return NULL;
        if (!expect(p, S_TOK_SEMI, 314)) return NULL;
        return s;
    }
    if (at(p, S_TOK_KW_CONTINUE)) {
        advance(p);
        s_stmt *s = stmt_alloc(p, S_STMT_CONTINUE, loc);
        if (!s) return NULL;
        if (!expect(p, S_TOK_SEMI, 315)) return NULL;
        return s;
    }

    if (at(p, S_TOK_KW_IF)) {
        advance(p);
        s_stmt *s = stmt_alloc(p, S_STMT_IF, loc);
        if (!s) return NULL;
        if (!enter_depth(p, 316)) return NULL;
        s->u.ifstmt.cond = parse_assignment(p);
        if (!s->u.ifstmt.cond) { leave_depth(p); return NULL; }
        s->u.ifstmt.then_block = parse_block(p);
        if (!s->u.ifstmt.then_block) { leave_depth(p); return NULL; }
        if (accept(p, S_TOK_KW_ELSE)) {
            if (at(p, S_TOK_KW_IF)) {
                s->u.ifstmt.else_block = parse_statement(p);
            } else {
                s->u.ifstmt.else_block = parse_block(p);
            }
            if (!s->u.ifstmt.else_block) { leave_depth(p); return NULL; }
        }
        leave_depth(p);
        return s;
    }

    if (at(p, S_TOK_KW_WHILE)) {
        advance(p);
        s_stmt *s = stmt_alloc(p, S_STMT_WHILE, loc);
        if (!s) return NULL;
        if (!enter_depth(p, 317)) return NULL;
        s->u.whilestmt.cond = parse_assignment(p);
        if (!s->u.whilestmt.cond) { leave_depth(p); return NULL; }
        s->u.whilestmt.body = parse_block(p);
        if (!s->u.whilestmt.body) { leave_depth(p); return NULL; }
        leave_depth(p);
        return s;
    }

    if (at(p, S_TOK_KW_FOR)) {
        advance(p);
        if (!at(p, S_TOK_IDENT)) {
            s_diag_error(p->diag, p->cur.loc, 318,
                         "expected loop variable after 'for'");
            return NULL;
        }
        s_stmt *s = stmt_alloc(p, S_STMT_FOR, loc);
        if (!s) return NULL;
        s->u.forstmt.name = ident_text(p);
        s->u.forstmt.var_index = -1;
        advance(p);
        if (!expect(p, S_TOK_KW_IN, 319)) return NULL;
        s->u.forstmt.iter = parse_assignment(p);
        if (!s->u.forstmt.iter) return NULL;
        if (!enter_depth(p, 320)) return NULL;
        s->u.forstmt.body = parse_block(p);
        if (!s->u.forstmt.body) { leave_depth(p); return NULL; }
        leave_depth(p);
        return s;
    }

    if (at(p, S_TOK_LBRACE)) {
        return parse_block(p);
    }

    /* expression statement */
    s_expr *e = parse_assignment(p);
    if (!e) return NULL;
    s_stmt *s = stmt_alloc(p, S_STMT_EXPR, loc);
    if (!s) return NULL;
    s->u.expr.expr = e;
    if (!expect(p, S_TOK_SEMI, 321)) return NULL;
    return s;
}

static s_stmt *parse_block(s_parser *p) {
    s_source_loc loc = p->cur.loc;
    if (!expect(p, S_TOK_LBRACE, 322)) return NULL;
    if (!enter_depth(p, 323)) return NULL;
    s_stmt *blk = stmt_alloc(p, S_STMT_BLOCK, loc);
    if (!blk) { leave_depth(p); return NULL; }
    s_stmt **items = NULL;
    s_size n = 0, cap = 0;
    while (!at(p, S_TOK_RBRACE) && !at(p, S_TOK_EOF)) {
        s_stmt *s = parse_statement(p);
        if (!s) { leave_depth(p); return NULL; }
        VEC_PUSH(p->arena, items, n, cap, sizeof(s_stmt *), s);
    }
    expect(p, S_TOK_RBRACE, 324);
    blk->u.block.items = items;
    blk->u.block.count = n;
    leave_depth(p);
    return blk;
}

/* ------------------------------------------------------------------ */
/* Top-level declarations                                              */
/* ------------------------------------------------------------------ */

static bool parse_func(s_parser *p) {
    s_source_loc loc = p->cur.loc;
    advance(p); /* 'func' */
    if (!at(p, S_TOK_IDENT)) {
        s_diag_error(p->diag, p->cur.loc, 330, "expected function name");
        return false;
    }
    s_func_decl fn;
    memset(&fn, 0, sizeof(fn));
    fn.name = ident_text(p);
    fn.loc = loc;
    advance(p);

    if (!expect(p, S_TOK_LPAREN, 331)) return false;
    s_param_entry *params = NULL;
    s_size n = 0, cap = 0;
    if (!at(p, S_TOK_RPAREN)) {
        for (;;) {
            if (!at(p, S_TOK_IDENT)) {
                s_diag_error(p->diag, p->cur.loc, 332, "expected parameter name");
                return false;
            }
            const char *pname = ident_text(p);
            advance(p);
            if (!expect(p, S_TOK_COLON, 333)) return false;
            s_type *pt = parse_type(p);
            if (!pt) return false;
            s_param_entry entry = { pname, pt };
            VEC_PUSH(p->arena, params, n, cap, sizeof(s_param_entry), entry);
            if (!accept(p, S_TOK_COMMA)) break;
        }
    }
    if (!expect(p, S_TOK_RPAREN, 334)) return false;

    fn.nparams = n;
    fn.params = s_arena_calloc(p->arena, n ? n : 1,
                               sizeof(s_func_param), _Alignof(s_func_param));
    if (!fn.params) return false;
    for (s_size i = 0; i < n; ++i) {
        fn.params[i].name = params[i].name;
        fn.params[i].type = params[i].type;
        fn.params[i].index = (int)i;
    }

    if (accept(p, S_TOK_ARROW)) {
        fn.ret_type = parse_type(p);
        if (!fn.ret_type) return false;
    } else {
        fn.ret_type = type_prim(p, S_TYPE_UNIT, loc);
        if (!fn.ret_type) return false;
    }
    fn.body = parse_block(p);
    if (!fn.body) return false;
    fn.func_index = (int)p->nfuncs;

    VEC_PUSH(p->arena, p->funcs, p->nfuncs, p->funcs_cap,
             sizeof(s_func_decl), fn);
    return true;
}

static bool parse_record(s_parser *p) {
    s_source_loc loc = p->cur.loc;
    advance(p); /* 'type' */
    if (!at(p, S_TOK_IDENT)) {
        s_diag_error(p->diag, p->cur.loc, 340, "expected record name");
        return false;
    }
    s_record_decl rec;
    memset(&rec, 0, sizeof(rec));
    rec.name = ident_text(p);
    rec.loc = loc;
    rec.index = (int)p->nrecords;
    advance(p);

    if (!expect(p, S_TOK_LBRACE, 341)) return false;
    s_field_decl *fields = NULL;
    s_size n = 0, cap = 0;
    while (!at(p, S_TOK_RBRACE) && !at(p, S_TOK_EOF)) {
        if (!expect(p, S_TOK_KW_LET, 342)) return false;
        if (!at(p, S_TOK_IDENT)) {
            s_diag_error(p->diag, p->cur.loc, 343, "expected field name");
            return false;
        }
        s_field_decl fd;
        memset(&fd, 0, sizeof(fd));
        fd.name = ident_text(p);
        fd.loc = p->cur.loc;
        fd.index = (int)n;
        advance(p);
        if (!expect(p, S_TOK_COLON, 344)) return false;
        fd.type = parse_type(p);
        if (!fd.type) return false;
        if (!expect(p, S_TOK_SEMI, 345)) return false;
        VEC_PUSH(p->arena, fields, n, cap, sizeof(s_field_decl), fd);
    }
    if (!expect(p, S_TOK_RBRACE, 346)) return false;
    rec.fields = fields;
    rec.nfields = n;
    VEC_PUSH(p->arena, p->records, p->nrecords, p->records_cap,
             sizeof(s_record_decl), rec);
    return true;
}

bool s_parse(s_parser *p, const char *file, const char *src, s_size len,
             s_arena *arena, s_diag_ctx *diag) {
    memset(p, 0, sizeof(*p));
    p->arena = arena;
    p->diag = diag;
    s_lexer_init(&p->lx, file, src, len, arena, diag);
    advance(p);
    p->mod.loc = p->cur.loc;
    p->mod.main_index = -1;

    bool ok = true;
    while (!at(p, S_TOK_EOF)) {
        if (at(p, S_TOK_KW_FUNC)) {
            if (!parse_func(p)) { ok = false; }
        } else if (at(p, S_TOK_KW_TYPE)) {
            if (!parse_record(p)) { ok = false; }
        } else {
            s_diag_error(diag, p->cur.loc, 350,
                         "unexpected %s at top level",
                         s_tok_kind_name(p->cur.kind));
            advance(p);
            ok = false;
        }
    }

    p->mod.funcs = p->funcs;
    p->mod.nfuncs = p->nfuncs;
    p->mod.records = p->records;
    p->mod.nrecords = p->nrecords;
    return ok;
}
