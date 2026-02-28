#include "../include/nodev.h"
#include "../include/vga.h"
#include "../include/vfs.h"
#include "../include/kstring.h"
#include "../include/keyboard.h"


//  Arena allocator  (replaces heap usage in the original code)

static const int NV_ARENA = 131072;   // 128 KB
static uint8_t   nv_mem[NV_ARENA];
static int       nv_top = 0;
static uint32_t  nv_iters = 0;        // iteration guard (infinite-loop protection)
static const uint32_t NV_MAX_ITERS = 200000;

static void* nv_alloc(int sz) {
    sz = (sz + 7) & ~7;
    if (nv_top + sz > NV_ARENA) {
        vga.setColor(LIGHT_RED, BLACK);
        vga.println("[NodeV] Arena full");
        vga.setColor(LIGHT_GREY, BLACK);
        return nullptr;
    }
    void* p = nv_mem + nv_top;
    k_memset(p, 0, sz);
    nv_top += sz;
    return p;
}


//  Lexer  (ported from lexer.cpp — same DFA, no std::set)

enum NVTok {
    T_EOF=0, T_NUM, T_FLOAT, T_STR, T_IDENT, T_KW, T_OP, T_SYM, T_ERR
};

struct Token {
    NVTok type;
    char  lex[64];
    int   inum;
    double fnum;
};

static const char* NV_KEYWORDS[] = {
    "int","float","string","return","if","else",
    "while","for","function","pout","pin","list",
    "true","false","class","new","public","private",
    "self","constructor","delete", nullptr
};
static bool is_kw(const char* s) {
    for (int i = 0; NV_KEYWORDS[i]; i++)
        if (k_strcmp(s, NV_KEYWORDS[i]) == 0) return true;
    return false;
}

struct Lexer {
    const char* src;
    int         pos;
    Token       cur;
};

static char lx_peek(Lexer* L) { return L->src[L->pos]; }
static char lx_get (Lexer* L) { return L->src[L->pos++]; }

static void lx_skip_ws(Lexer* L) {
    for (;;) {
        while (L->src[L->pos] == ' ' || L->src[L->pos] == '\t' ||
               L->src[L->pos] == '\n' || L->src[L->pos] == '\r')
            L->pos++;
        if (L->src[L->pos] == '/' && L->src[L->pos+1] == '/') {
            while (L->src[L->pos] && L->src[L->pos] != '\n') L->pos++;
        } else break;
    }
}

static void lx_next(Lexer* L) {
    lx_skip_ws(L);
    Token& t = L->cur;
    t.inum = 0; t.fnum = 0.0; t.lex[0] = '\0';

    char c = lx_peek(L);
    if (!c) { t.type = T_EOF; return; }

    // Identifiers / keywords
    if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        int i = 0;
        while ((lx_peek(L) == '_' || (lx_peek(L) >= 'a' && lx_peek(L) <= 'z') ||
                (lx_peek(L) >= 'A' && lx_peek(L) <= 'Z') ||
                (lx_peek(L) >= '0' && lx_peek(L) <= '9')) && i < 63)
            t.lex[i++] = lx_get(L);
        t.lex[i] = '\0';
        t.type = is_kw(t.lex) ? T_KW : T_IDENT;
        return;
    }

    // Numbers (int or float)
    if (c >= '0' && c <= '9') {
        int i = 0; bool is_float = false;
        while (lx_peek(L) >= '0' && lx_peek(L) <= '9' && i < 63)
            t.lex[i++] = lx_get(L);
        if (lx_peek(L) == '.') {
            is_float = true;
            t.lex[i++] = lx_get(L);
            while (lx_peek(L) >= '0' && lx_peek(L) <= '9' && i < 63)
                t.lex[i++] = lx_get(L);
        }
        t.lex[i] = '\0';
        if (is_float) {
            // Simple atof
            double val = 0.0, frac = 0.1; bool inFrac = false;
            for (int j = 0; t.lex[j]; j++) {
                if (t.lex[j] == '.') { inFrac = true; continue; }
                int d = t.lex[j] - '0';
                if (!inFrac) val = val * 10 + d;
                else { val += d * frac; frac *= 0.1; }
            }
            t.fnum = val; t.type = T_FLOAT;
        } else {
            t.inum = k_atoi(t.lex); t.type = T_NUM;
        }
        return;
    }

    // String literals
    if (c == '"') {
        lx_get(L);
        int i = 0;
        while (lx_peek(L) && lx_peek(L) != '"' && i < 63) {
            char ch = lx_get(L);
            if (ch == '\\') {
                char esc = lx_get(L);
                if (esc == 'n') ch = '\n';
                else if (esc == 't') ch = '\t';
                else ch = esc;
            }
            t.lex[i++] = ch;
        }
        if (lx_peek(L) == '"') lx_get(L);
        t.lex[i] = '\0'; t.type = T_STR;
        return;
    }

    // Operators (2-char first)
    lx_get(L);
    char n = lx_peek(L);
    t.lex[0] = c; t.lex[1] = '\0'; t.type = T_OP;

    if ((c=='=' || c=='!' || c=='<' || c=='>') && n == '=') {
        t.lex[1] = lx_get(L); t.lex[2] = '\0'; return;
    }
    if (c == '<' && n == '<') { t.lex[1] = lx_get(L); t.lex[2] = '\0'; return; }
    if (c == '>' && n == '>') { t.lex[1] = lx_get(L); t.lex[2] = '\0'; return; }
    if (c == '&' && n == '&') { t.lex[1] = lx_get(L); t.lex[2] = '\0'; return; }
    if (c == '|' && n == '|') { t.lex[1] = lx_get(L); t.lex[2] = '\0'; return; }

    // Single-char operators
    if (c=='+' || c=='-' || c=='*' || c=='/' || c=='%' ||
        c=='^' || c=='~' || c=='&' || c=='|' || c=='=' ||
        c=='<' || c=='>' || c=='!') return; // T_OP already set

    // Symbols
    if (c=='(' || c==')' || c=='{' || c=='}' ||
        c=='[' || c==']' || c==';' || c==',' || c=='.') {
        t.type = T_SYM; return;
    }
    t.type = T_ERR;
}

static void lx_init(Lexer* L, const char* src) { L->src = src; L->pos = 0; lx_next(L); }
static Token lx_eat(Lexer* L)  { Token t = L->cur; lx_next(L); return t; }
static bool  lx_is(Lexer* L, NVTok tp, const char* v = nullptr) {
    if (L->cur.type != tp) return false;
    if (v) return k_strcmp(L->cur.lex, v) == 0;
    return true;
}
static Token lx_expect(Lexer* L, NVTok tp, const char* v = nullptr) {
    if (!lx_is(L, tp, v)) {
        vga.setColor(LIGHT_RED, BLACK);
        vga.print("[NodeV] Syntax error near '"); vga.print(L->cur.lex); vga.println("'");
        vga.setColor(LIGHT_GREY, BLACK);
    }
    return lx_eat(L);
}


//  AST  (tagged structs replace the virtual class hierarchy)

enum EK {
    EK_NUM=0, EK_FLT, EK_STR, EK_BOOL, EK_VAR,
    EK_BIN, EK_UNI, EK_CALL, EK_IDX, EK_METH,
    EK_PROP, EK_NEW, EK_SELF
};
enum SK {
    SK_DECL=0, SK_LIST, SK_ASSIGN, SK_IASGN, SK_PASGN,
    SK_RET, SK_IF, SK_WHILE, SK_FOR, SK_FUNC,
    SK_CLASS, SK_PRINT, SK_EXPR, SK_DEL, SK_INPUT
};

struct NExpr {
    EK     kind;
    int    ival;          // EK_NUM, EK_BOOL
    double fval;          // EK_FLT
    char   sval[64];      // EK_STR value, EK_VAR/CALL/METH/PROP/NEW name
    char   sval2[32];     // EK_METH method, EK_PROP prop
    char   op[4];         // EK_BIN/UNI
    NExpr* left;          // EK_BIN left, EK_UNI/IDX subject
    NExpr* right;         // EK_BIN right, EK_IDX index
    NExpr* args[8];
    int    argc;
};
struct NStmt {
    SK     kind;
    char   name[32];      // var/func/class name
    char   name2[32];     // prop name (SK_PASGN)
    NExpr* expr;          // init, assign, return, expr stmt
    NExpr* cond;
    NExpr* idx;           // SK_IASGN index
    NExpr* pargs[8];      // SK_PRINT args
    int    pargc;
    NStmt** body;         // then/while/for/func body
    int    bodyc;
    NStmt** els;          // else branch
    int    elsc;
    char   params[6][24]; // func params
    int    paramc;
    NStmt* finit;
    NStmt* fupdate;
};

static NExpr* mk_expr(EK k) { NExpr* e = (NExpr*)nv_alloc(sizeof(NExpr)); if(e) e->kind=k; return e; }
static NStmt* mk_stmt(SK k) { NStmt* s = (NStmt*)nv_alloc(sizeof(NStmt)); if(s) s->kind=k; return s; }

// Allocate a body array of n pointers
static NStmt** mk_body(int n) { return (NStmt**)nv_alloc(n * (int)sizeof(NStmt*)); }


//  Parser  (recursive descent — mirrors parser.h structure)

struct Parser { Lexer* L; };

static NExpr* p_expr(Parser* P);
static NStmt* p_stmt(Parser* P);

// body: collect statements until '}' or EOF
static void p_body(Parser* P, NStmt**& out, int& outc) {
    NStmt* buf[64]; outc = 0;
    while (!lx_is(P->L, T_SYM, "}") && !lx_is(P->L, T_EOF)) {
        NStmt* s = p_stmt(P);
        if (s && outc < 64) buf[outc++] = s;
    }
    if (lx_is(P->L, T_SYM, "}")) lx_eat(P->L);
    out = mk_body(outc);
    if (out) k_memcpy(out, buf, outc * sizeof(NStmt*));
}

// arg list: expr (',' expr)*
static int p_args(Parser* P, NExpr** args, int max) {
    int c = 0;
    if (lx_is(P->L, T_SYM, ")")) return 0;
    while (c < max) {
        args[c++] = p_expr(P);
        if (!lx_is(P->L, T_SYM, ",")) break;
        lx_eat(P->L);
    }
    return c;
}

// factor (atomic expressions + unary) — mirrors parser.cpp factor()
static NExpr* p_factor(Parser* P) {
    Token t = P->L->cur;

    // Grouped expression
    if (lx_is(P->L, T_SYM, "(")) {
        lx_eat(P->L); NExpr* e = p_expr(P);
        lx_expect(P->L, T_SYM, ")"); return e;
    }
    // Unary
    if (lx_is(P->L, T_OP, "!") || lx_is(P->L, T_OP, "-") || lx_is(P->L, T_OP, "~")) {
        lx_eat(P->L);
        NExpr* e = mk_expr(EK_UNI);
        if (e) { k_strcpy(e->op, t.lex); e->left = p_factor(P); }
        return e;
    }
    // true/false
    if (lx_is(P->L, T_KW, "true"))  { lx_eat(P->L); NExpr* e=mk_expr(EK_BOOL); if(e) e->ival=1; return e; }
    if (lx_is(P->L, T_KW, "false")) { lx_eat(P->L); NExpr* e=mk_expr(EK_BOOL); if(e) e->ival=0; return e; }
    // self
    if (lx_is(P->L, T_KW, "self"))  { lx_eat(P->L); return mk_expr(EK_SELF); }
    // Number
    if (lx_is(P->L, T_NUM))  { lx_eat(P->L); NExpr* e=mk_expr(EK_NUM);  if(e) e->ival=t.inum; return e; }
    if (lx_is(P->L, T_FLOAT)){ lx_eat(P->L); NExpr* e=mk_expr(EK_FLT);  if(e) e->fval=t.fnum; return e; }
    // String
    if (lx_is(P->L, T_STR))  { lx_eat(P->L); NExpr* e=mk_expr(EK_STR);  if(e) k_strncpy(e->sval,t.lex,63); return e; }
    // new ClassName(args)
    if (lx_is(P->L, T_KW, "new")) {
        lx_eat(P->L);
        Token cls = lx_expect(P->L, T_IDENT);
        lx_expect(P->L, T_SYM, "(");
        NExpr* e = mk_expr(EK_NEW);
        if (e) { k_strncpy(e->sval, cls.lex, 63); e->argc = p_args(P, e->args, 8); }
        lx_expect(P->L, T_SYM, ")");
        return e;
    }
    // pin(var) — read input into expr (used in expr context)
    if (lx_is(P->L, T_KW, "pin")) {
        lx_eat(P->L); lx_expect(P->L, T_SYM, "(");
        Token v = lx_expect(P->L, T_IDENT);
        lx_expect(P->L, T_SYM, ")");
        // Treat pin as a var expr (interpreter handles it specially)
        NExpr* e = mk_expr(EK_VAR);
        if (e) { k_strncpy(e->sval, v.lex, 63); }
        return e;
    }
    // Identifier: could be var, call, array index, prop access, method call
    if (lx_is(P->L, T_IDENT)) {
        lx_eat(P->L);
        // function call: name(args)
        if (lx_is(P->L, T_SYM, "(")) {
            lx_eat(P->L);
            NExpr* e = mk_expr(EK_CALL);
            if (e) { k_strncpy(e->sval, t.lex, 63); e->argc = p_args(P, e->args, 8); }
            lx_expect(P->L, T_SYM, ")");
            return e;
        }
        // array index: name[expr]
        if (lx_is(P->L, T_SYM, "[")) {
            lx_eat(P->L);
            NExpr* idx = p_expr(P); lx_expect(P->L, T_SYM, "]");
            NExpr* e = mk_expr(EK_IDX);
            if (e) { k_strncpy(e->sval, t.lex, 63); e->right = idx; }
            return e;
        }
        // prop or method: name.prop  or  name.method(args)
        if (lx_is(P->L, T_SYM, ".")) {
            lx_eat(P->L);
            Token prop = lx_expect(P->L, T_IDENT);
            if (lx_is(P->L, T_SYM, "(")) {
                lx_eat(P->L);
                NExpr* e = mk_expr(EK_METH);
                if (e) {
                    k_strncpy(e->sval,  t.lex,    63);
                    k_strncpy(e->sval2, prop.lex,  31);
                    e->argc = p_args(P, e->args, 8);
                }
                lx_expect(P->L, T_SYM, ")");
                return e;
            }
            NExpr* e = mk_expr(EK_PROP);
            if (e) { k_strncpy(e->sval, t.lex, 63); k_strncpy(e->sval2, prop.lex, 31); }
            return e;
        }
        // plain variable
        NExpr* e = mk_expr(EK_VAR);
        if (e) k_strncpy(e->sval, t.lex, 63);
        return e;
    }
    return nullptr;
}

// Multiplicative: factor (* / %) factor
static NExpr* p_term(Parser* P) {
    NExpr* left = p_factor(P);
    while (lx_is(P->L, T_OP, "*") || lx_is(P->L, T_OP, "/") || lx_is(P->L, T_OP, "%")) {
        Token op = lx_eat(P->L);
        NExpr* e = mk_expr(EK_BIN);
        if (e) { k_strcpy(e->op, op.lex); e->left = left; e->right = p_factor(P); }
        left = e;
    }
    return left;
}
// Additive
static NExpr* p_add(Parser* P) {
    NExpr* left = p_term(P);
    while (lx_is(P->L, T_OP, "+") || lx_is(P->L, T_OP, "-")) {
        Token op = lx_eat(P->L);
        NExpr* e = mk_expr(EK_BIN);
        if (e) { k_strcpy(e->op, op.lex); e->left = left; e->right = p_term(P); }
        left = e;
    }
    return left;
}
// Shift
static NExpr* p_shift(Parser* P) {
    NExpr* left = p_add(P);
    while (lx_is(P->L, T_OP, "<<") || lx_is(P->L, T_OP, ">>")) {
        Token op = lx_eat(P->L);
        NExpr* e = mk_expr(EK_BIN);
        if (e) { k_strcpy(e->op, op.lex); e->left = left; e->right = p_add(P); }
        left = e;
    }
    return left;
}
// Comparison + equality
static NExpr* p_cmp(Parser* P) {
    NExpr* left = p_shift(P);
    while (lx_is(P->L,T_OP,"<")||lx_is(P->L,T_OP,">")||lx_is(P->L,T_OP,"<=")||
           lx_is(P->L,T_OP,">=")||lx_is(P->L,T_OP,"==")||lx_is(P->L,T_OP,"!=")) {
        Token op = lx_eat(P->L);
        NExpr* e = mk_expr(EK_BIN);
        if (e) { k_strcpy(e->op, op.lex); e->left = left; e->right = p_shift(P); }
        left = e;
    }
    return left;
}
// Bitwise AND / XOR / OR
static NExpr* p_bitwise(Parser* P) {
    NExpr* left = p_cmp(P);
    while (lx_is(P->L,T_OP,"&")||lx_is(P->L,T_OP,"^")||lx_is(P->L,T_OP,"|")) {
        Token op = lx_eat(P->L);
        NExpr* e = mk_expr(EK_BIN);
        if (e) { k_strcpy(e->op, op.lex); e->left = left; e->right = p_cmp(P); }
        left = e;
    }
    return left;
}
// Logical AND / OR — top-level expression
static NExpr* p_expr(Parser* P) {
    NExpr* left = p_bitwise(P);
    while (lx_is(P->L,T_OP,"&&")||lx_is(P->L,T_OP,"||")) {
        Token op = lx_eat(P->L);
        NExpr* e = mk_expr(EK_BIN);
        if (e) { k_strcpy(e->op, op.lex); e->left = left; e->right = p_bitwise(P); }
        left = e;
    }
    return left;
}

// Statement parser
static NStmt* p_stmt(Parser* P) {
    Token t = P->L->cur;

    // Variable declaration: int/float/string IDENT (= expr)? ;
    if (lx_is(P->L,T_KW,"int")||lx_is(P->L,T_KW,"float")||lx_is(P->L,T_KW,"string")) {
        lx_eat(P->L);
        Token name = lx_expect(P->L, T_IDENT);
        NStmt* s = mk_stmt(SK_DECL);
        if (s) {
            k_strncpy(s->name, name.lex, 31);
            if (lx_is(P->L, T_OP, "=")) { lx_eat(P->L); s->expr = p_expr(P); }
        }
        lx_expect(P->L, T_SYM, ";"); return s;
    }
    // List declaration: list IDENT ;
    if (lx_is(P->L, T_KW, "list")) {
        lx_eat(P->L);
        Token name = lx_expect(P->L, T_IDENT);
        NStmt* s = mk_stmt(SK_LIST);
        if (s) k_strncpy(s->name, name.lex, 31);
        lx_expect(P->L, T_SYM, ";"); return s;
    }
    // return
    if (lx_is(P->L, T_KW, "return")) {
        lx_eat(P->L);
        NStmt* s = mk_stmt(SK_RET);
        if (s && !lx_is(P->L, T_SYM, ";")) s->expr = p_expr(P);
        lx_expect(P->L, T_SYM, ";"); return s;
    }
    // delete
    if (lx_is(P->L, T_KW, "delete")) {
        lx_eat(P->L);
        Token name = lx_expect(P->L, T_IDENT);
        NStmt* s = mk_stmt(SK_DEL);
        if (s) k_strncpy(s->name, name.lex, 31);
        lx_expect(P->L, T_SYM, ";"); return s;
    }
    // if
    if (lx_is(P->L, T_KW, "if")) {
        lx_eat(P->L); lx_expect(P->L, T_SYM, "(");
        NExpr* cond = p_expr(P); lx_expect(P->L, T_SYM, ")");
        lx_expect(P->L, T_SYM, "{");
        NStmt* s = mk_stmt(SK_IF);
        if (s) {
            s->cond = cond;
            p_body(P, s->body, s->bodyc);
            if (lx_is(P->L, T_KW, "else")) {
                lx_eat(P->L); lx_expect(P->L, T_SYM, "{");
                p_body(P, s->els, s->elsc);
            }
        }
        return s;
    }
    // while
    if (lx_is(P->L, T_KW, "while")) {
        lx_eat(P->L); lx_expect(P->L, T_SYM, "(");
        NExpr* cond = p_expr(P); lx_expect(P->L, T_SYM, ")");
        lx_expect(P->L, T_SYM, "{");
        NStmt* s = mk_stmt(SK_WHILE);
        if (s) { s->cond = cond; p_body(P, s->body, s->bodyc); }
        return s;
    }
    // for (NodeV style): for (int i = 0, i < 10, i = i + 1) { }
    if (lx_is(P->L, T_KW, "for")) {
        lx_eat(P->L); lx_expect(P->L, T_SYM, "(");
        // optional "int" before init var
        if (lx_is(P->L, T_KW, "int")) lx_eat(P->L);
        // init: var = expr
        Token ivar = lx_expect(P->L, T_IDENT);
        lx_expect(P->L, T_OP, "=");
        NExpr* initVal = p_expr(P);
        NStmt* initStmt = mk_stmt(SK_DECL);
        if (initStmt) { k_strncpy(initStmt->name, ivar.lex, 31); initStmt->expr = initVal; }
        lx_expect(P->L, T_SYM, ",");
        // condition
        NExpr* cond = p_expr(P);
        lx_expect(P->L, T_SYM, ",");
        // update: var = expr
        Token uvar = lx_expect(P->L, T_IDENT);
        lx_expect(P->L, T_OP, "=");
        NExpr* updVal = p_expr(P);
        NStmt* updStmt = mk_stmt(SK_ASSIGN);
        if (updStmt) { k_strncpy(updStmt->name, uvar.lex, 31); updStmt->expr = updVal; }
        lx_expect(P->L, T_SYM, ")"); lx_expect(P->L, T_SYM, "{");
        NStmt* s = mk_stmt(SK_FOR);
        if (s) { s->cond = cond; s->finit = initStmt; s->fupdate = updStmt;
                 p_body(P, s->body, s->bodyc); }
        return s;
    }
    // function
    if (lx_is(P->L, T_KW, "function")) {
        lx_eat(P->L);
        Token name = lx_expect(P->L, T_IDENT);
        lx_expect(P->L, T_SYM, "(");
        NStmt* s = mk_stmt(SK_FUNC);
        if (s) {
            k_strncpy(s->name, name.lex, 31);
            while (!lx_is(P->L, T_SYM, ")") && s->paramc < 6) {
                Token p = lx_expect(P->L, T_IDENT);
                k_strncpy(s->params[s->paramc++], p.lex, 23);
                if (lx_is(P->L, T_SYM, ",")) lx_eat(P->L);
            }
            lx_expect(P->L, T_SYM, ")"); lx_expect(P->L, T_SYM, "{");
            p_body(P, s->body, s->bodyc);
        }
        return s;
    }
    // pout (print)
    if (lx_is(P->L, T_KW, "pout")) {
        lx_eat(P->L); lx_expect(P->L, T_SYM, "(");
        NStmt* s = mk_stmt(SK_PRINT);
        if (s) {
            while (!lx_is(P->L, T_SYM, ")") && s->pargc < 8) {
                s->pargs[s->pargc++] = p_expr(P);
                if (lx_is(P->L, T_SYM, ",")) lx_eat(P->L);
            }
        }
        lx_expect(P->L, T_SYM, ")"); lx_expect(P->L, T_SYM, ";"); return s;
    }
    // pin(var) — input statement
    if (lx_is(P->L, T_KW, "pin")) {
        lx_eat(P->L); lx_expect(P->L, T_SYM, "(");
        Token v = lx_expect(P->L, T_IDENT);
        NStmt* s = mk_stmt(SK_INPUT);
        if (s) k_strncpy(s->name, v.lex, 31);
        lx_expect(P->L, T_SYM, ")"); lx_expect(P->L, T_SYM, ";"); return s;
    }

    // Identifier-led statements: assign, array assign, prop assign, expr stmt
    if (lx_is(P->L, T_IDENT)) {
        Token id = lx_eat(P->L);

        // obj.prop = expr ;
        if (lx_is(P->L, T_SYM, ".")) {
            lx_eat(P->L);
            Token prop = lx_expect(P->L, T_IDENT);
            if (lx_is(P->L, T_OP, "=")) {
                lx_eat(P->L);
                NStmt* s = mk_stmt(SK_PASGN);
                if (s) { k_strncpy(s->name, id.lex, 31); k_strncpy(s->name2, prop.lex, 31);
                         s->expr = p_expr(P); }
                lx_expect(P->L, T_SYM, ";"); return s;
            }
            // method call statement: obj.method(args);
            if (lx_is(P->L, T_SYM, "(")) {
                lx_eat(P->L);
                NExpr* e = mk_expr(EK_METH);
                if (e) { k_strncpy(e->sval, id.lex, 63); k_strncpy(e->sval2, prop.lex, 31);
                         e->argc = p_args(P, e->args, 8); }
                lx_expect(P->L, T_SYM, ")"); lx_expect(P->L, T_SYM, ";");
                NStmt* s = mk_stmt(SK_EXPR); if (s) s->expr = e; return s;
            }
        }
        // arr[idx] = expr ;
        if (lx_is(P->L, T_SYM, "[")) {
            lx_eat(P->L); NExpr* idx = p_expr(P); lx_expect(P->L, T_SYM, "]");
            lx_expect(P->L, T_OP, "=");
            NStmt* s = mk_stmt(SK_IASGN);
            if (s) { k_strncpy(s->name, id.lex, 31); s->idx = idx; s->expr = p_expr(P); }
            lx_expect(P->L, T_SYM, ";"); return s;
        }
        // var = expr ;
        if (lx_is(P->L, T_OP, "=")) {
            lx_eat(P->L);
            NStmt* s = mk_stmt(SK_ASSIGN);
            if (s) { k_strncpy(s->name, id.lex, 31); s->expr = p_expr(P); }
            lx_expect(P->L, T_SYM, ";"); return s;
        }
        // func call as statement: f(args);
        if (lx_is(P->L, T_SYM, "(")) {
            lx_eat(P->L);
            NExpr* e = mk_expr(EK_CALL);
            if (e) { k_strncpy(e->sval, id.lex, 63); e->argc = p_args(P, e->args, 8); }
            lx_expect(P->L, T_SYM, ")"); lx_expect(P->L, T_SYM, ";");
            NStmt* s = mk_stmt(SK_EXPR); if (s) s->expr = e; return s;
        }
        // bare identifier — skip
        lx_expect(P->L, T_SYM, ";");
    }

    // Skip unknown tokens to avoid infinite loops
    if (!lx_is(P->L, T_EOF)) lx_eat(P->L);
    return nullptr;
}


//  Value  (mirrors interpreter.h Value struct)

enum NVType { NVT_INT=0, NVT_FLOAT, NVT_STRING };
struct NVValue {
    NVType type;
    int    i;
    double f;
    char   s[64];
};
static NVValue nv_int(int v)          { NVValue r; r.type=NVT_INT;   r.i=v; r.f=0; r.s[0]='\0'; return r; }
static NVValue nv_float(double v)     { NVValue r; r.type=NVT_FLOAT; r.f=v; r.i=0; r.s[0]='\0'; return r; }
static NVValue nv_str(const char* v)  { NVValue r; r.type=NVT_STRING;r.i=0; r.f=0; k_strncpy(r.s,v,63); return r; }

static bool nv_truthy(NVValue v) {
    if (v.type==NVT_INT)    return v.i != 0;
    if (v.type==NVT_FLOAT)  return v.f != 0.0;
    if (v.type==NVT_STRING) return v.s[0] != '\0';
    return false;
}
static void nv_tostr(NVValue v, char* buf, int sz) {
    if (v.type==NVT_STRING) { k_strncpy(buf, v.s, sz-1); buf[sz-1]='\0'; return; }
    if (v.type==NVT_INT)    { k_itoa(v.i, buf, 10); return; }
    // float: simple conversion
    int whole = (int)v.f;
    int frac  = (int)((v.f - whole) * 1000);
    if (frac < 0) frac = -frac;
    char tmp[32]; k_itoa(whole, tmp, 10);
    k_strcpy(buf, tmp); k_strcat(buf, ".");
    char ftmp[8]; k_itoa(frac, ftmp, 10); k_strcat(buf, ftmp);
}


//  Environment  (replaces unordered_map scopes in interpreter.h)

struct NVVar  { char name[32]; NVValue val; bool used; };
struct NVScope {
    NVVar   vars[24];
    int     vc;
    NVScope* parent;
};
struct NVFunc { char name[32]; NStmt* decl; bool used; };
struct NVList { char name[32]; NVValue items[64]; int count; bool used; };

static NVScope* nv_new_scope(NVScope* parent) {
    NVScope* s = (NVScope*)nv_alloc(sizeof(NVScope));
    if (s) { s->vc = 0; s->parent = parent; }
    return s;
}
static NVValue* nv_find_var(NVScope* sc, const char* name) {
    for (NVScope* c = sc; c; c = c->parent)
        for (int i = 0; i < c->vc; i++)
            if (c->vars[i].used && k_strcmp(c->vars[i].name, name) == 0)
                return &c->vars[i].val;
    return nullptr;
}
static void nv_set_var(NVScope* sc, const char* name, NVValue v) {
    // Update existing
    for (NVScope* c = sc; c; c = c->parent)
        for (int i = 0; i < c->vc; i++)
            if (c->vars[i].used && k_strcmp(c->vars[i].name, name) == 0) {
                c->vars[i].val = v; return;
            }
    // Create in current scope
    if (sc && sc->vc < 24) {
        k_strncpy(sc->vars[sc->vc].name, name, 31);
        sc->vars[sc->vc].val = v; sc->vars[sc->vc].used = true; sc->vc++;
    }
}

static NVFunc  nv_funcs[16];
static NVList  nv_lists[8];
static bool    nv_ret = false;
static NVValue nv_ret_val;

static NVFunc* nv_find_func(const char* name) {
    for (int i = 0; i < 16; i++)
        if (nv_funcs[i].used && k_strcmp(nv_funcs[i].name, name) == 0) return &nv_funcs[i];
    return nullptr;
}
static NVList* nv_find_list(const char* name) {
    for (int i = 0; i < 8; i++)
        if (nv_lists[i].used && k_strcmp(nv_lists[i].name, name) == 0) return &nv_lists[i];
    return nullptr;
}


//  Interpreter  (mirrors interpreter.cpp, no dynamic_cast)

static NVValue nv_eval(NExpr* e, NVScope* sc);
static void    nv_exec(NStmt* s, NVScope* sc);

static NVValue nv_eval(NExpr* e, NVScope* sc) {
    if (!e) return nv_int(0);
    if (++nv_iters > NV_MAX_ITERS) {
        vga.setColor(LIGHT_RED, BLACK); vga.println("[NodeV] Iteration limit hit");
        vga.setColor(LIGHT_GREY, BLACK); nv_ret = true; nv_ret_val = nv_int(-1);
        return nv_int(0);
    }
    switch (e->kind) {
    case EK_NUM:  return nv_int(e->ival);
    case EK_FLT:  return nv_float(e->fval);
    case EK_STR:  return nv_str(e->sval);
    case EK_BOOL: return nv_int(e->ival);
    case EK_SELF: return nv_int(0); // OOP not implemented in kernel version
    case EK_VAR: {
        NVValue* v = nv_find_var(sc, e->sval);
        return v ? *v : nv_int(0);
    }
    case EK_IDX: {
        NVList* lst = nv_find_list(e->sval);
        if (lst) {
            NVValue idx = nv_eval(e->right, sc);
            int i = idx.type==NVT_INT ? idx.i : (int)idx.f;
            if (i >= 0 && i < lst->count) return lst->items[i];
        }
        // String indexing
        NVValue* sv = nv_find_var(sc, e->sval);
        if (sv && sv->type == NVT_STRING) {
            NVValue idx = nv_eval(e->right, sc);
            int i = idx.type==NVT_INT ? idx.i : (int)idx.f;
            if (i >= 0 && i < (int)k_strlen(sv->s)) {
                char ch[2] = { sv->s[i], '\0' }; return nv_str(ch);
            }
        }
        return nv_int(0);
    }
    case EK_UNI: {
        NVValue v = nv_eval(e->left, sc);
        if (k_strcmp(e->op, "-") == 0)
            return v.type==NVT_FLOAT ? nv_float(-v.f) : nv_int(-v.i);
        if (k_strcmp(e->op, "!") == 0) return nv_int(!nv_truthy(v));
        if (k_strcmp(e->op, "~") == 0 && v.type==NVT_INT) return nv_int(~v.i);
        return v;
    }
    case EK_BIN: {
        NVValue l = nv_eval(e->left, sc);
        NVValue r = nv_eval(e->right, sc);
        const char* op = e->op;

        // String ops (mirrors interpreter.cpp string operations)
        if (l.type==NVT_STRING || r.type==NVT_STRING) {
            if (k_strcmp(op,"+")==0) {
                char a[64], b[64], res[128];
                nv_tostr(l,a,64); nv_tostr(r,b,64);
                k_strncpy(res,a,63); k_strcat(res,b);
                return nv_str(res);
            }
            if (k_strcmp(op,"==")==0) return nv_int(k_strcmp(l.s,r.s)==0);
            if (k_strcmp(op,"!=")==0) return nv_int(k_strcmp(l.s,r.s)!=0);
            if (k_strcmp(op,"<")==0)  return nv_int(k_strcmp(l.s,r.s)<0);
            if (k_strcmp(op,">")==0)  return nv_int(k_strcmp(l.s,r.s)>0);
        }

        // Numeric ops — mirrors interpreter.cpp exactly
        bool useF = (l.type==NVT_FLOAT || r.type==NVT_FLOAT);
        double lf = l.type==NVT_INT ? (double)l.i : l.f;
        double rf = r.type==NVT_INT ? (double)r.i : r.f;
        int    li = l.type==NVT_INT ? l.i : (int)l.f;
        int    ri = r.type==NVT_INT ? r.i : (int)r.f;

        if (k_strcmp(op,"+")==0)  return useF ? nv_float(lf+rf) : nv_int(li+ri);
        if (k_strcmp(op,"-")==0)  return useF ? nv_float(lf-rf) : nv_int(li-ri);
        if (k_strcmp(op,"*")==0)  return useF ? nv_float(lf*rf) : nv_int(li*ri);
        if (k_strcmp(op,"/")==0)  { if(rf==0){vga.println("[NodeV] Div/0");return nv_int(0);}
                                    return nv_float(lf/rf); }
        if (k_strcmp(op,"%")==0)  { if(ri==0){vga.println("[NodeV] Mod/0");return nv_int(0);}
                                    return nv_int(li%ri); }
        if (k_strcmp(op,"<")==0)  return useF ? nv_int(lf< rf) : nv_int(li< ri);
        if (k_strcmp(op,">")==0)  return useF ? nv_int(lf> rf) : nv_int(li> ri);
        if (k_strcmp(op,"<=")==0) return useF ? nv_int(lf<=rf) : nv_int(li<=ri);
        if (k_strcmp(op,">=")==0) return useF ? nv_int(lf>=rf) : nv_int(li>=ri);
        if (k_strcmp(op,"==")==0) return useF ? nv_int(lf==rf) : nv_int(li==ri);
        if (k_strcmp(op,"!=")==0) return useF ? nv_int(lf!=rf) : nv_int(li!=ri);
        if (k_strcmp(op,"&&")==0) return nv_int(nv_truthy(l) && nv_truthy(r));
        if (k_strcmp(op,"||")==0) return nv_int(nv_truthy(l) || nv_truthy(r));
        if (k_strcmp(op,"&")==0)  return nv_int(li & ri);
        if (k_strcmp(op,"|")==0)  return nv_int(li | ri);
        if (k_strcmp(op,"^")==0)  return nv_int(li ^ ri);
        if (k_strcmp(op,"<<")==0) return nv_int(li << ri);
        if (k_strcmp(op,">>")==0) return nv_int(li >> ri);
        return nv_int(0);
    }
    case EK_CALL: {
        NVFunc* fn = nv_find_func(e->sval);
        if (!fn) {
            vga.setColor(LIGHT_RED,BLACK);
            vga.print("[NodeV] Undefined function '"); vga.print(e->sval); vga.println("'");
            vga.setColor(LIGHT_GREY,BLACK); return nv_int(0);
        }
        NStmt* fd = fn->decl;
        // Evaluate args
        NVValue av[8]; int ac = e->argc < 8 ? e->argc : 8;
        for (int i = 0; i < ac; i++) av[i] = nv_eval(e->args[i], sc);
        // New scope
        NVScope* fsc = nv_new_scope(nullptr); // functions have isolated scope
        if (!fsc) return nv_int(0);
        int pc = fd->paramc < ac ? fd->paramc : ac;
        for (int i = 0; i < pc; i++) nv_set_var(fsc, fd->params[i], av[i]);
        // Execute body
        bool prev_ret = nv_ret; nv_ret = false;
        for (int i = 0; i < fd->bodyc && !nv_ret; i++) nv_exec(fd->body[i], fsc);
        NVValue ret = nv_ret ? nv_ret_val : nv_int(0);
        nv_ret = prev_ret; // restore outer return state
        return ret;
    }
    case EK_PROP:
    case EK_METH:
    case EK_NEW:
        // OOP is not implemented in the kernel NodeV version
        return nv_int(0);
    }
    return nv_int(0);
}

static void nv_exec(NStmt* s, NVScope* sc) {
    if (!s || nv_ret) return;
    if (++nv_iters > NV_MAX_ITERS) { nv_ret = true; nv_ret_val = nv_int(-1); return; }

    switch (s->kind) {
    case SK_DECL:
        nv_set_var(sc, s->name, s->expr ? nv_eval(s->expr, sc) : nv_int(0));
        break;
    case SK_LIST: {
        for (int i = 0; i < 8; i++) if (!nv_lists[i].used) {
            k_strncpy(nv_lists[i].name, s->name, 31);
            nv_lists[i].count = 0; nv_lists[i].used = true; break;
        }
        break;
    }
    case SK_ASSIGN:
        nv_set_var(sc, s->name, nv_eval(s->expr, sc));
        break;
    case SK_IASGN: {
        NVList* lst = nv_find_list(s->name);
        if (lst) {
            NVValue idx = nv_eval(s->idx, sc);
            int i = idx.type==NVT_INT ? idx.i : (int)idx.f;
            if (i >= 0 && i < 64) {
                if (i >= lst->count) lst->count = i + 1;
                lst->items[i] = nv_eval(s->expr, sc);
            }
        }
        break;
    }
    case SK_PASGN: break; // OOP not in kernel version
    case SK_DEL:   break; // OOP not in kernel version
    case SK_RET:
        nv_ret_val = s->expr ? nv_eval(s->expr, sc) : nv_int(0);
        nv_ret = true;
        break;
    case SK_IF: {
        NVScope* child = nv_new_scope(sc);
        if (!child) break;
        if (nv_truthy(nv_eval(s->cond, sc))) {
            for (int i = 0; i < s->bodyc && !nv_ret; i++) nv_exec(s->body[i], child);
        } else {
            for (int i = 0; i < s->elsc && !nv_ret; i++) nv_exec(s->els[i], child);
        }
        break;
    }
    case SK_WHILE: {
        while (!nv_ret && nv_truthy(nv_eval(s->cond, sc))) {
            NVScope* child = nv_new_scope(sc);
            if (!child) break;
            for (int i = 0; i < s->bodyc && !nv_ret; i++) nv_exec(s->body[i], child);
        }
        break;
    }
    case SK_FOR: {
        NVScope* fsc = nv_new_scope(sc);
        if (!fsc) break;
        nv_exec(s->finit, fsc);
        while (!nv_ret && nv_truthy(nv_eval(s->cond, fsc))) {
            NVScope* child = nv_new_scope(fsc);
            if (!child) break;
            for (int i = 0; i < s->bodyc && !nv_ret; i++) nv_exec(s->body[i], child);
            nv_exec(s->fupdate, fsc);
        }
        break;
    }
    case SK_FUNC: {
        for (int i = 0; i < 16; i++) if (!nv_funcs[i].used) {
            k_strncpy(nv_funcs[i].name, s->name, 31);
            nv_funcs[i].decl = s; nv_funcs[i].used = true; break;
        }
        break;
    }
    case SK_CLASS: break; // OOP not in kernel version
    case SK_PRINT: {
        for (int i = 0; i < s->pargc; i++) {
            NVValue v = nv_eval(s->pargs[i], sc);
            char buf[128]; nv_tostr(v, buf, 128);
            vga.print(buf);
        }
        break;
    }
    case SK_INPUT: {
        static char ibuf[64];
        ibuf[0] = '\0';
        keyboard_readline(ibuf, 64);
        nv_set_var(sc, s->name, nv_str(ibuf));
        break;
    }
    case SK_EXPR:
        nv_eval(s->expr, sc);
        break;
    }
}


//  Public API

NodeVResult nodev_exec(const char* source) {
    // Reset arena and state
    nv_top = 0; nv_iters = 0; nv_ret = false;
    k_memset(nv_funcs, 0, sizeof(nv_funcs));
    k_memset(nv_lists, 0, sizeof(nv_lists));

    Lexer lx; lx_init(&lx, source);
    Parser P; P.L = &lx;

    // Pass 1: hoist functions (mirrors interpreter.cpp pass 1)
    NVScope* global = nv_new_scope(nullptr);
    if (!global) return NV_ERROR;

    // Collect all statements
    NStmt* stmts[128]; int sc = 0;
    while (!lx_is(P.L, T_EOF) && sc < 128) {
        NStmt* st = p_stmt(&P);
        if (st) {
            stmts[sc++] = st;
            if (st->kind == SK_FUNC) { // hoist
                for (int i = 0; i < 16; i++) if (!nv_funcs[i].used) {
                    k_strncpy(nv_funcs[i].name, st->name, 31);
                    nv_funcs[i].decl = st; nv_funcs[i].used = true; break;
                }
            }
        }
    }

    // Pass 2: execute (mirrors interpreter.cpp pass 2)
    for (int i = 0; i < sc && !nv_ret; i++) {
        if (stmts[i]->kind == SK_FUNC) continue; // already hoisted
        nv_exec(stmts[i], global);
    }

    return NV_OK;
}

NodeVResult nodev_run_file(const char* filename) {
    static char src[VFS_MAX_FILESIZE];
    if (vfs_read(filename, src, VFS_MAX_FILESIZE) < 0) {
        vga.setColor(LIGHT_RED, BLACK);
        vga.print("[NodeV] File not found: "); vga.println(filename);
        vga.setColor(LIGHT_GREY, BLACK);
        return NV_ERROR;
    }
    return nodev_exec(src);
}
