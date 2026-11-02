#ifndef SURU_AST_H
#define SURU_AST_H

#include "suru/base.h"
#include "suru/diag.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

typedef enum s_type_kind {
    S_TYPE_INT,
    S_TYPE_FLOAT,
    S_TYPE_BOOL,
    S_TYPE_STR,
    S_TYPE_UNIT,
    S_TYPE_ANY,           /* wildcard: matches any type (builtin args only) */
    S_TYPE_ARRAY,
    S_TYPE_FN,
    S_TYPE_RECORD,        /* named record reference (name resolved by sema) */
    S_TYPE_VOID,          /* only used internally for missing returns */
} s_type_kind;

typedef struct s_type {
    s_type_kind kind;
    s_source_loc loc;
    struct s_type *elem;        /* array element type */
    struct s_type **params;     /* fn parameter types */
    s_size nparams;
    struct s_type *ret;         /* fn return type */
    const char *record_name;    /* record reference */
    int record_index;           /* resolved record decl index (sema) */
} s_type;

typedef struct s_expr s_expr;
typedef struct s_stmt s_stmt;

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

typedef enum s_expr_kind {
    S_EXPR_INT_LIT,
    S_EXPR_FLOAT_LIT,
    S_EXPR_STR_LIT,
    S_EXPR_BOOL_LIT,
    S_EXPR_NONE_LIT,
    S_EXPR_ARRAY_LIT,
    S_EXPR_VAR,               /* identifier reference */
    S_EXPR_CONST_REF,         /* a named constant used as a value */
    S_EXPR_FUNC_REF,          /* a named function used as a value */
    S_EXPR_CALL,
    S_EXPR_RECORD_INIT,
    S_EXPR_INDEX,
    S_EXPR_FIELD,
    S_EXPR_UNARY,             /* -x, not x */
    S_EXPR_BINARY,            /* arithmetic / comparison / logic */
    S_EXPR_ASSIGN,            /* = */
    S_EXPR_COMPOUND_ASSIGN,   /* += -= *= /= */
} s_expr_kind;

typedef enum s_bin_op {
    S_BIN_ADD, S_BIN_SUB, S_BIN_MUL, S_BIN_DIV, S_BIN_MOD,
    S_BIN_EQ, S_BIN_NE, S_BIN_LT, S_BIN_LE, S_BIN_GT, S_BIN_GE,
    S_BIN_AND, S_BIN_OR,
} s_bin_op;

typedef enum s_un_op {
    S_UN_NEG, S_UN_NOT,
} s_un_op;

struct s_expr {
    s_expr_kind kind;
    s_source_loc loc;
    s_type *type;                 /* filled by sema */
    /* is in an rvalue context (sema) */
    bool is_lvalue;
    union {
        s_i64 i;
        double f;
        struct { const char *text; s_size len; } str;
        bool b;
        struct { const char *name; int var_index; bool is_mut; } var;
        struct { int const_index; } cref;
        struct { s_expr *callee; s_expr **args; s_size nargs; } call;
        struct {
            const char *name;      /* record type name */
            int record_index;      /* resolved by sema */
            const char **fnames;   /* provided field names */
            s_expr **fvals;        /* provided field value expressions */
            s_size nprovided;
            s_expr **by_field;     /* resolved values indexed by field index */
            s_size nfields;
        } rec;
        struct { s_expr *obj; s_expr *index; } index;
        struct { s_expr *obj; const char *field; int field_index; } field;
        struct { s_un_op op; s_expr *operand; } unary;
        struct { s_bin_op op; s_expr *lhs; s_expr *rhs; } binary;
        struct { s_expr **elems; s_size count; } array;
        struct { s_expr *lhs; s_expr *rhs; } assign;
        struct { s_bin_op op; s_expr *lhs; s_expr *rhs; } compound;
    } u;
};

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

typedef enum s_stmt_kind {
    S_STMT_LET,
    S_STMT_EXPR,
    S_STMT_RETURN,
    S_STMT_IF,
    S_STMT_WHILE,
    S_STMT_FOR,
    S_STMT_BREAK,
    S_STMT_CONTINUE,
    S_STMT_BLOCK,
} s_stmt_kind;

struct s_stmt {
    s_stmt_kind kind;
    s_source_loc loc;
    union {
        struct {
            bool is_mut;
            const char *name;
            int var_index;
            s_type *decl_type;    /* may be NULL if inferred */
            s_expr *init;
        } let;
        struct { s_expr *expr; } expr;
        struct { s_expr *expr; bool has_expr; } ret;
        struct {
            s_expr *cond;
            s_stmt *then_block;
            s_stmt *else_block;   /* may be NULL; else-if chains flatten */
            s_type *result_type;
        } ifstmt;
        struct { s_expr *cond; s_stmt *body; } whilestmt;
        struct {
            const char *name;
            int var_index;
            s_type *decl_type;
            s_expr *iter;
            s_stmt *body;
        } forstmt;
        struct { s_stmt **items; s_size count; } block;
    } u;
};

/* ------------------------------------------------------------------ */
/* Declarations                                                        */
/* ------------------------------------------------------------------ */

typedef struct s_field_decl {
    const char *name;
    s_type *type;
    int index;
    s_source_loc loc;
} s_field_decl;

typedef struct s_func_param {
    const char *name;
    s_type *type;
    int index;                   /* local var index in frame */
} s_func_param;

typedef struct s_func_decl {
    const char *name;
    s_func_param *params;
    s_size nparams;
    s_type *ret_type;
    s_stmt *body;                /* a BLOCK statement */
    int func_index;
    s_u32 max_frame_slots;       /* semi for stack size */
    s_u32 max_operand_depth;     /* sema */
    s_source_loc loc;
} s_func_decl;

typedef struct s_record_decl {
    const char *name;
    s_field_decl *fields;
    s_size nfields;
    int index;
    s_source_loc loc;
} s_record_decl;

typedef struct s_const_decl {
    const char *name;
    s_type *type;                /* resolved type */
    int lit_kind;                /* S_EXPR_INT_LIT / FLOAT / STR / BOOL */
    union {
        s_i64 i;
        double f;
        struct { const char *text; s_size len; } str;
        bool b;
    } v;
    int index;
    s_source_loc loc;
} s_const_decl;

typedef struct s_module {
    s_func_decl *funcs;
    s_size nfuncs;
    s_record_decl *records;
    s_size nrecords;
    s_const_decl *consts;
    s_size nconsts;
    int main_index;              /* -1 if absent */
    s_source_loc loc;
} s_module;

#ifdef __cplusplus
}
#endif

#endif /* SURU_AST_H */
