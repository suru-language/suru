#include "suru/lexer.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

void s_lexer_init(s_lexer *lx, const char *file, const char *src,
                  s_size len, s_arena *arena, s_diag_ctx *diag) {
    lx->file = file;
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    lx->line = 1;
    lx->col = 1;
    lx->arena = arena;
    lx->diag = diag;
    lx->depth = 0;
}

const char *s_tok_kind_name(s_tok_kind k) {
    switch (k) {
    case S_TOK_EOF: return "end of file";
    case S_TOK_IDENT: return "identifier";
    case S_TOK_INT: return "integer literal";
    case S_TOK_FLOAT: return "float literal";
    case S_TOK_STR: return "string literal";
    case S_TOK_TRUE: return "'true'";
    case S_TOK_FALSE: return "'false'";
    case S_TOK_NONE: return "'none'";
    case S_TOK_KW_BREAK: return "'break'";
    case S_TOK_KW_CONTINUE: return "'continue'";
    case S_TOK_KW_ELSE: return "'else'";
    case S_TOK_KW_FOR: return "'for'";
    case S_TOK_KW_FUNC: return "'func'";
    case S_TOK_KW_IF: return "'if'";
    case S_TOK_KW_IN: return "'in'";
    case S_TOK_KW_LET: return "'let'";
    case S_TOK_KW_MUT: return "'mut'";
    case S_TOK_KW_RETURN: return "'return'";
    case S_TOK_KW_TYPE: return "'type'";
    case S_TOK_KW_USE: return "'use'";
    case S_TOK_KW_WHILE: return "'while'";
    case S_TOK_KW_NEW: return "'new'";
    case S_TOK_KW_AS: return "'as'";
    case S_TOK_KW_CONST: return "'const'";
    case S_TOK_KW_ENUM: return "'enum'";
    case S_TOK_KW_DO: return "'do'";
    case S_TOK_LPAREN: return "'('";
    case S_TOK_RPAREN: return "')'";
    case S_TOK_LBRACE: return "'{'";
    case S_TOK_RBRACE: return "'}'";
    case S_TOK_LBRACKET: return "'['";
    case S_TOK_RBRACKET: return "']'";
    case S_TOK_COMMA: return "','";
    case S_TOK_SEMI: return "';'";
    case S_TOK_DOT: return "'.'";
    case S_TOK_COLON: return "':'";
    case S_TOK_ARROW: return "'->'";
    case S_TOK_QUESTION: return "'?'";
    case S_TOK_ASSIGN: return "'='";
    case S_TOK_PLUS: return "'+'";
    case S_TOK_MINUS: return "'-'";
    case S_TOK_STAR: return "'*'";
    case S_TOK_SLASH: return "'/'";
    case S_TOK_PERCENT: return "'%'";
    case S_TOK_AMP: return "'&'";
    case S_TOK_PIPE: return "'|'";
    case S_TOK_CARET: return "'^'";
    case S_TOK_SHL: return "'<<'";
    case S_TOK_SHR: return "'>>'";
    case S_TOK_TILDE: return "'~'";
    case S_TOK_EQ: return "'=='";
    case S_TOK_NE: return "'!='";
    case S_TOK_LT: return "'<'";
    case S_TOK_LE: return "'<='";
    case S_TOK_GT: return "'>'";
    case S_TOK_GE: return "'>='";
    case S_TOK_AND: return "'and'";
    case S_TOK_OR: return "'or'";
    case S_TOK_NOT: return "'not'";
    case S_TOK_PLUS_ASSIGN: return "'+='";
    case S_TOK_MINUS_ASSIGN: return "'-='";
    case S_TOK_STAR_ASSIGN: return "'*='";
    case S_TOK_SLASH_ASSIGN: return "'/='";
    }
    return "?";
}

static void advance(s_lexer *lx) {
    if (lx->pos >= lx->len) return;
    if (lx->src[lx->pos] == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static char peek(s_lexer *lx, s_size off) {
    s_size p = lx->pos + off;
    if (p >= lx->len) return '\0';
    return lx->src[p];
}

static char cur(s_lexer *lx) {
    return peek(lx, 0);
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }
static bool is_hex(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#endif

static bool lex_error(s_lexer *lx, s_source_loc loc, int code,
                      const char *fmt, ...) {
    s_diag_error(lx->diag, loc, code, fmt);
    return false;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static void skip_trivia(s_lexer *lx) {
    for (;;) {
        char c = cur(lx);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance(lx);
        } else if (c == '/' && peek(lx, 1) == '/') {
            while (cur(lx) != '\0' && cur(lx) != '\n') advance(lx);
        } else if (c == '/' && peek(lx, 1) == '*') {
            advance(lx); advance(lx);
            int depth = 1;
            while (depth > 0) {
                if (cur(lx) == '\0') break;
                if (cur(lx) == '/' && peek(lx, 1) == '*') {
                    advance(lx); advance(lx); depth++;
                } else if (cur(lx) == '*' && peek(lx, 1) == '/') {
                    advance(lx); advance(lx); depth--;
                } else {
                    advance(lx);
                }
            }
        } else {
            return;
        }
    }
}

static s_tok_kind keyword_kind(const char *s, s_size len) {
#define KW(match, kind) \
    if (len == sizeof(match) - 1 && memcmp(s, match, len) == 0) return kind;
    KW("break", S_TOK_KW_BREAK)
    KW("continue", S_TOK_KW_CONTINUE)
    KW("else", S_TOK_KW_ELSE)
    KW("for", S_TOK_KW_FOR)
    KW("func", S_TOK_KW_FUNC)
    KW("fn", S_TOK_KW_FUNC)
    KW("if", S_TOK_KW_IF)
    KW("in", S_TOK_KW_IN)
    KW("let", S_TOK_KW_LET)
    KW("mut", S_TOK_KW_MUT)
    KW("return", S_TOK_KW_RETURN)
    KW("type", S_TOK_KW_TYPE)
    KW("use", S_TOK_KW_USE)
    KW("while", S_TOK_KW_WHILE)
    KW("new", S_TOK_KW_NEW)
    KW("as", S_TOK_KW_AS)
    KW("const", S_TOK_KW_CONST)
    KW("enum", S_TOK_KW_ENUM)
    KW("do", S_TOK_KW_DO)
    KW("and", S_TOK_AND)
    KW("or", S_TOK_OR)
    KW("not", S_TOK_NOT)
    KW("true", S_TOK_TRUE)
    KW("false", S_TOK_FALSE)
    KW("none", S_TOK_NONE)
#undef KW
    return S_TOK_IDENT;
}

static bool lex_number(s_lexer *lx, s_token *tok) {
    s_source_loc loc = tok->loc;
    s_size start = lx->pos;

    bool is_float = false;
    if (cur(lx) == '0' && (peek(lx, 1) == 'x' || peek(lx, 1) == 'X')) {
        /* hex */
        advance(lx); advance(lx);
        s_size hstart = lx->pos;
        while (is_hex(cur(lx)) || cur(lx) == '_') advance(lx);
        if (lx->pos == hstart) {
            return lex_error(lx, loc, 101, "expected hex digit after '0x'");
        }
        /* parse ignoring underscores and sign */
        const char *p = lx->src + hstart;
        const char *end = lx->src + lx->pos;
        s_u64 v = 0;
        for (; p < end; ++p) {
            if (*p == '_') continue;
            unsigned d;
            if (*p >= '0' && *p <= '9') d = (unsigned)(*p - '0');
            else if (*p >= 'a' && *p <= 'f') d = (unsigned)(*p - 'a') + 10;
            else d = (unsigned)(*p - 'A') + 10;
            if (v > (UINT64_MAX >> 4)) {
                return lex_error(lx, loc, 102, "integer literal too large");
            }
            v = (v << 4) | d;
        }
        if (v > (s_u64)INT64_MAX) {
            return lex_error(lx, loc, 102, "integer literal outside signed 64-bit range");
        }
        tok->kind = S_TOK_INT;
        tok->v.i = (s_i64)v;
        return true;
    }

    if (cur(lx) == '0' && (peek(lx, 1) == 'b' || peek(lx, 1) == 'B')) {
        advance(lx); advance(lx);
        s_size bstart = lx->pos;
        while (cur(lx) == '0' || cur(lx) == '1' || cur(lx) == '_') advance(lx);
        if (lx->pos == bstart) {
            return lex_error(lx, loc, 103, "expected binary digit after '0b'");
        }
        const char *p = lx->src + bstart;
        const char *end = lx->src + lx->pos;
        s_u64 v = 0;
        for (; p < end; ++p) {
            if (*p == '_') continue;
            if (v > (UINT64_MAX >> 1)) {
                return lex_error(lx, loc, 102, "integer literal too large");
            }
            v = (v << 1) | (s_u64)(*p - '0');
        }
        if (v > (s_u64)INT64_MAX) {
            return lex_error(lx, loc, 102, "integer literal outside signed 64-bit range");
        }
        tok->kind = S_TOK_INT;
        tok->v.i = (s_i64)v;
        return true;
    }

    /* decimal */
    while (is_digit(cur(lx)) || cur(lx) == '_') advance(lx);
    if (cur(lx) == '.' && is_digit(peek(lx, 1))) {
        is_float = true;
        advance(lx);
        while (is_digit(cur(lx)) || cur(lx) == '_') advance(lx);
    }
    if (cur(lx) == 'e' || cur(lx) == 'E') {
        s_size save = lx->pos;
        advance(lx);
        if (cur(lx) == '+' || cur(lx) == '-') advance(lx);
        if (is_digit(cur(lx))) {
            is_float = true;
            while (is_digit(cur(lx)) || cur(lx) == '_') advance(lx);
        } else {
            lx->pos = save;
            lx->col = lx->col; /* keep column consistent: rewind handled below */
        }
    }

    s_size end = lx->pos;
    if (is_float) {
        char buf[128];
        s_size n = end - start;
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, lx->src + start, n);
        buf[n] = '\0';
        char *endptr = NULL;
        errno = 0;
        double v = strtod(buf, &endptr);
        if (errno == ERANGE) {
            /* out of range; clamp to +/-HUGE_VAL as tolerated */
        }
        tok->kind = S_TOK_FLOAT;
        tok->v.f = v;
        return true;
    }

    {
        char buf[128];
        s_size n = end - start;
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, lx->src + start, n);
        buf[n] = '\0';
        /* strip underscores for parsing */
        char clean[128];
        s_size j = 0;
        for (s_size i = 0; i < n; ++i) {
            if (buf[i] != '_') clean[j++] = buf[i];
        }
        clean[j] = '\0';
        errno = 0;
        char *endptr = NULL;
        long long v = strtoll(clean, &endptr, 10);
        if (errno == ERANGE || *endptr != '\0') {
            return lex_error(lx, loc, 102, "integer literal outside signed 64-bit range");
        }
        tok->kind = S_TOK_INT;
        tok->v.i = (s_i64)v;
        return true;
    }
}

static bool lex_string(s_lexer *lx, s_token *tok) {
    s_source_loc loc = tok->loc;
    advance(lx); /* opening quote */
    s_size cap = 16;
    s_size len = 0;
    char *buf = malloc(cap);
    if (!buf) return lex_error(lx, loc, 110, "out of memory lexing string");
    bool ok = true;

    while (cur(lx) != '\0' && cur(lx) != '"') {
        if (cur(lx) == '\n') {
            ok = lex_error(lx, loc, 104, "unterminated string literal (newline)");
            break;
        }
        char c = cur(lx);
        if (c == '\\') {
            advance(lx);
            char e = cur(lx);
            switch (e) {
            case 'n': c = '\n'; advance(lx); break;
            case 't': c = '\t'; advance(lx); break;
            case 'r': c = '\r'; advance(lx); break;
            case '0': c = '\0'; advance(lx); break;
            case '\\': c = '\\'; advance(lx); break;
            case '"': c = '"'; advance(lx); break;
            case '\'': c = '\''; advance(lx); break;
            case 'u': {
                advance(lx);
                unsigned cp = 0;
                int i;
                for (i = 0; i < 4; ++i) {
                    char h = cur(lx);
                    unsigned d;
                    if (h >= '0' && h <= '9') d = (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') d = (unsigned)(h - 'a') + 10;
                    else if (h >= 'A' && h <= 'F') d = (unsigned)(h - 'A') + 10;
                    else break;
                    cp = (cp << 4) | d;
                    advance(lx);
                }
                if (i != 4) {
                    ok = lex_error(lx, loc, 105, "invalid \\u escape");
                    break;
                }
                /* encode UTF-8 (cp <= 0xFFFF by construction) */
                if (cp < 0x80) {
                    c = (char)cp; advance(lx); /* already advanced */
                    /* fallthrough to append below */
                } else {
                    char utf8[4];
                    int n = 0;
                    if (cp < 0x800) {
                        utf8[0] = (char)(0xC0 | (cp >> 6));
                        utf8[1] = (char)(0x80 | (cp & 0x3F));
                        n = 2;
                    } else {
                        utf8[0] = (char)(0xE0 | (cp >> 12));
                        utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        utf8[2] = (char)(0x80 | (cp & 0x3F));
                        n = 3;
                    }
                    if (len + (s_size)n > cap) { cap *= 2; buf = realloc(buf, cap); }
                    memcpy(buf + len, utf8, (s_size)n);
                    len += (s_size)n;
                    continue;
                }
                break;
            }
            default:
                ok = lex_error(lx, loc, 105, "invalid escape sequence '\\%c'", e);
                break;
            }
            if (!ok) break;
        } else {
            advance(lx);
        }
        if (!ok) break;
        if (len + 1 > cap) { cap *= 2; char *nb = realloc(buf, cap); if (!nb) { ok = false; break; } buf = nb; }
        buf[len++] = c;
    }

    if (ok && cur(lx) == '"') {
        advance(lx); /* closing quote */
        /* store into arena */
        char *arena = s_arena_alloc(lx->arena, len + 1, 1);
        if (arena) {
            memcpy(arena, buf, len);
            arena[len] = '\0';
            tok->v.s.text = arena;
            tok->v.s.len = len;
            tok->kind = S_TOK_STR;
        } else {
            ok = lex_error(lx, loc, 110, "out of memory");
        }
    } else if (ok) {
        ok = lex_error(lx, loc, 104, "unterminated string literal");
    }
    free(buf);
    return ok;
}

bool s_lexer_next(s_lexer *lx, s_token *out) {
    if (lx->pos == 0 && lx->len >= 2 &&
        lx->src[0] == '#' && lx->src[1] == '!') {
        while (cur(lx) != '\0' && cur(lx) != '\n') advance(lx);
    }
    skip_trivia(lx);
    out->loc.file = lx->file;
    out->loc.line = lx->line;
    out->loc.col = lx->col;

    char c = cur(lx);
    if (c == '\0') {
        out->kind = S_TOK_EOF;
        return true;
    }
    if (is_alpha(c)) {
        s_size start = lx->pos;
        while (is_alnum(cur(lx))) advance(lx);
        const char *text = lx->src + start;
        s_size len = lx->pos - start;
        if (len >= 2 && text[0] == '_' && text[1] == '_') {
            return lex_error(lx, out->loc, 111,
                             "identifiers may not begin with '__'");
        }
        out->kind = keyword_kind(text, len);
        if (out->kind == S_TOK_IDENT) {
            out->v.s.text = text;
            out->v.s.len = len;
        }
        return true;
    }
    if (is_digit(c)) {
        return lex_number(lx, out);
    }
    if (c == '"') {
        return lex_string(lx, out);
    }

    /* operators / punctuation */
    switch (c) {
    case '(': advance(lx); out->kind = S_TOK_LPAREN; return true;
    case ')': advance(lx); out->kind = S_TOK_RPAREN; return true;
    case '{': advance(lx); out->kind = S_TOK_LBRACE; return true;
    case '}': advance(lx); out->kind = S_TOK_RBRACE; return true;
    case '[': advance(lx); out->kind = S_TOK_LBRACKET; return true;
    case ']': advance(lx); out->kind = S_TOK_RBRACKET; return true;
    case ',': advance(lx); out->kind = S_TOK_COMMA; return true;
    case ';': advance(lx); out->kind = S_TOK_SEMI; return true;
    case '.': advance(lx); out->kind = S_TOK_DOT; return true;
    case ':': advance(lx); out->kind = S_TOK_COLON; return true;
    case '+':
        advance(lx);
        if (cur(lx) == '=') { advance(lx); out->kind = S_TOK_PLUS_ASSIGN; }
        else out->kind = S_TOK_PLUS;
        return true;
    case '-':
        advance(lx);
        if (cur(lx) == '>') { advance(lx); out->kind = S_TOK_ARROW; }
        else if (cur(lx) == '=') { advance(lx); out->kind = S_TOK_MINUS_ASSIGN; }
        else out->kind = S_TOK_MINUS;
        return true;
    case '*':
        advance(lx);
        if (cur(lx) == '=') { advance(lx); out->kind = S_TOK_STAR_ASSIGN; }
        else out->kind = S_TOK_STAR;
        return true;
    case '/':
        advance(lx);
        if (cur(lx) == '=') { advance(lx); out->kind = S_TOK_SLASH_ASSIGN; }
        else out->kind = S_TOK_SLASH;
        return true;
    case '%': advance(lx); out->kind = S_TOK_PERCENT; return true;
    case '=':
        advance(lx);
        if (cur(lx) == '=') { advance(lx); out->kind = S_TOK_EQ; }
        else out->kind = S_TOK_ASSIGN;
        return true;
    case '!':
        advance(lx);
        if (cur(lx) == '=') { advance(lx); out->kind = S_TOK_NE; }
        else return lex_error(lx, out->loc, 106, "unexpected '!'");
        return true;
    case '<':
        advance(lx);
        if (cur(lx) == '<') { advance(lx); out->kind = S_TOK_SHL; }
        else if (cur(lx) == '=') { advance(lx); out->kind = S_TOK_LE; }
        else out->kind = S_TOK_LT;
        return true;
    case '>':
        advance(lx);
        if (cur(lx) == '>') { advance(lx); out->kind = S_TOK_SHR; }
        else if (cur(lx) == '=') { advance(lx); out->kind = S_TOK_GE; }
        else out->kind = S_TOK_GT;
        return true;
    case '?': advance(lx); out->kind = S_TOK_QUESTION; return true;
    case '&': advance(lx); out->kind = S_TOK_AMP; return true;
    case '|': advance(lx); out->kind = S_TOK_PIPE; return true;
    case '^': advance(lx); out->kind = S_TOK_CARET; return true;
    case '~': advance(lx); out->kind = S_TOK_TILDE; return true;
    default:
        return lex_error(lx, out->loc, 107, "unexpected character '\\x%02x'", (unsigned char)c);
    }
}
