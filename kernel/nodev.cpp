// nodev.cpp — NodeV scripting language, NodOS kernel edition v2
//
// Complete freestanding implementation of the NodeV language.
// Merges the hosted desktop version (ast.h / lexer / parser / interpreter)
// into a single self-contained file that compiles under:
//   g++ -m32 -ffreestanding -fno-exceptions -fno-rtti -fno-use-cxa-atexit
//
// Features vs v1:
//   + Full OOP: class, new, constructor, self, delete, public/private
//   + self.prop = val  assignment inside methods
//   + Longer strings (up to 128 chars)
//   + More function params (up to 8)
//   + More functions (up to 32)
//   + More object instances (heap of 32 objects)
//   + $import "file.nod" directive (resolved via VFS)
//   + Cleaner error messages via shell_print

#include "../include/nodev.h"
#include "../include/shell.h"
#include "../include/vfs.h"
#include "../include/kstring.h"

// ═════════════════════════════════════════════════════════════════════════════
//  Arena allocator  — all AST nodes, scopes, objects come from here.
//  Reset at the start of every nodev_exec() call.
// ═════════════════════════════════════════════════════════════════════════════

static const int NV_ARENA = 196608;   // 192 KB
static uint8_t   nv_mem[NV_ARENA];
static int       nv_top  = 0;

static void* nv_alloc(int sz) {
    sz = (sz + 7) & ~7;
    if (nv_top + sz > NV_ARENA) {
        shell_println("[NodeV] Out of arena memory");
        return nullptr;
    }
    void* p = nv_mem + nv_top;
    k_memset(p, 0, sz);
    nv_top += sz;
    return p;
}

// Iteration guard — prevents infinite loops from hanging the kernel
static uint32_t nv_iters = 0;
static const uint32_t NV_MAX_ITERS = 300000;

// ═════════════════════════════════════════════════════════════════════════════
//  Lexer
// ═════════════════════════════════════════════════════════════════════════════

enum NVTok {
    T_EOF=0, T_NUM, T_FLOAT, T_STR, T_IDENT, T_KW, T_OP, T_SYM, T_ERR
};

struct Token {
    NVTok  type;
    char   lex[128];   // up from 64 — needed for longer identifiers/strings
    int    inum;
    double fnum;
};

static const char* NV_KEYWORDS[] = {
    "int","float","string","return","if","else",
    "while","for","function","pout","pin","list",
    "true","false","class","new","public","private",
    "self","constructor","destructor","delete",
    "import", nullptr
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

static char lx_peek(Lexer* L)          { return L->src[L->pos]; }
static char lx_peek2(Lexer* L)         { return L->src[L->pos] ? L->src[L->pos+1] : '\0'; }
static char lx_get (Lexer* L)          { return L->src[L->pos++]; }

static void lx_skip_ws(Lexer* L) {
    for (;;) {
        while (L->src[L->pos]==' '||L->src[L->pos]=='\t'||
               L->src[L->pos]=='\n'||L->src[L->pos]=='\r')
            L->pos++;
        // Line comments
        if (L->src[L->pos]=='/' && L->src[L->pos+1]=='/') {
            while (L->src[L->pos] && L->src[L->pos]!='\n') L->pos++;
        } else break;
    }
}

static void lx_next(Lexer* L) {
    lx_skip_ws(L);
    Token& t = L->cur;
    t.inum=0; t.fnum=0.0; t.lex[0]='\0';

    char c = lx_peek(L);
    if (!c) { t.type=T_EOF; return; }

    // Identifiers / keywords
    if (c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z')) {
        int i=0;
        while ((lx_peek(L)=='_'||
               (lx_peek(L)>='a'&&lx_peek(L)<='z')||
               (lx_peek(L)>='A'&&lx_peek(L)<='Z')||
               (lx_peek(L)>='0'&&lx_peek(L)<='9')) && i<127)
            t.lex[i++]=lx_get(L);
        t.lex[i]='\0';
        t.type = is_kw(t.lex) ? T_KW : T_IDENT;
        return;
    }

    // Numbers
    if (c>='0' && c<='9') {
        int i=0; bool is_float=false;
        while (lx_peek(L)>='0' && lx_peek(L)<='9' && i<127)
            t.lex[i++]=lx_get(L);
        if (lx_peek(L)=='.') {
            is_float=true; t.lex[i++]=lx_get(L);
            while (lx_peek(L)>='0' && lx_peek(L)<='9' && i<127)
                t.lex[i++]=lx_get(L);
        }
        t.lex[i]='\0';
        if (is_float) {
            double val=0.0,frac=0.1; bool inF=false;
            for (int j=0;t.lex[j];j++) {
                if (t.lex[j]=='.'){inF=true;continue;}
                int d=t.lex[j]-'0';
                if (!inF) val=val*10+d;
                else { val+=d*frac; frac*=0.1; }
            }
            t.fnum=val; t.type=T_FLOAT;
        } else {
            t.inum=k_atoi(t.lex); t.type=T_NUM;
        }
        return;
    }

    // String literals  (supports \n \t \\)
    if (c=='"') {
        lx_get(L);
        int i=0;
        while (lx_peek(L) && lx_peek(L)!='"' && i<127) {
            char ch=lx_get(L);
            if (ch=='\\') {
                char esc=lx_get(L);
                if      (esc=='n') ch='\n';
                else if (esc=='t') ch='\t';
                else if (esc=='\\') ch='\\';
                else ch=esc;
            }
            t.lex[i++]=ch;
        }
        if (lx_peek(L)=='"') lx_get(L);
        t.lex[i]='\0'; t.type=T_STR;
        return;
    }

    // Operators (2-char checked first)
    lx_get(L);
    char n=lx_peek(L);
    t.lex[0]=c; t.lex[1]='\0'; t.type=T_OP;

    if ((c=='='||c=='!'||c=='<'||c=='>') && n=='=') {
        t.lex[1]=lx_get(L); t.lex[2]='\0'; return;
    }
    if (c=='<'&&n=='<') { t.lex[1]=lx_get(L); t.lex[2]='\0'; return; }
    if (c=='>'&&n=='>') { t.lex[1]=lx_get(L); t.lex[2]='\0'; return; }
    if (c=='&'&&n=='&') { t.lex[1]=lx_get(L); t.lex[2]='\0'; return; }
    if (c=='|'&&n=='|') { t.lex[1]=lx_get(L); t.lex[2]='\0'; return; }

    if (c=='+'||c=='-'||c=='*'||c=='/'||c=='%'||
        c=='^'||c=='~'||c=='&'||c=='|'||c=='='||
        c=='<'||c=='>'||c=='!') return; // T_OP already set

    if (c=='('||c==')'||c=='{'||c=='}'||
        c=='['||c==']'||c==';'||c==','||c=='.') {
        t.type=T_SYM; return;
    }
    t.type=T_ERR;
}

static void  lx_init(Lexer* L, const char* src) { L->src=src; L->pos=0; lx_next(L); }
static Token lx_eat (Lexer* L)  { Token t=L->cur; lx_next(L); return t; }

static bool lx_is(Lexer* L, NVTok tp, const char* v=nullptr) {
    if (L->cur.type!=tp) return false;
    if (v) return k_strcmp(L->cur.lex,v)==0;
    return true;
}
static Token lx_expect(Lexer* L, NVTok tp, const char* v=nullptr) {
    if (!lx_is(L,tp,v)) {
        shell_print("[NodeV] Syntax error near '");
        shell_print(L->cur.lex);
        shell_println("'");
    }
    return lx_eat(L);
}

// ═════════════════════════════════════════════════════════════════════════════
//  AST  — tagged-union structs; no virtual, no dynamic_cast
// ═════════════════════════════════════════════════════════════════════════════

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
    char   sval[128];     // name / string value
    char   sval2[64];     // method / prop name
    char   op[4];         // EK_BIN / EK_UNI operator
    NExpr* left;
    NExpr* right;
    NExpr* args[8];
    int    argc;
};

// Class method descriptor stored inside ClassDecl
struct NMethod {
    char    name[32];
    bool    is_public;
    // params & body are stored directly in the method's NStmt (SK_FUNC)
    struct NStmt* decl;
};

struct NStmt {
    SK      kind;
    char    name[64];     // var / func / class name
    char    name2[64];    // prop name for SK_PASGN
    NExpr*  expr;
    NExpr*  cond;
    NExpr*  idx;
    NExpr*  pargs[8];
    int     pargc;
    NStmt** body;         // then / while / for / func body
    int     bodyc;
    NStmt** els;
    int     elsc;
    char    params[8][32]; // function params (up to 8)
    int     paramc;
    NStmt*  finit;
    NStmt*  fupdate;

    // Class declaration extra data
    NMethod  methods[16];
    int      methodc;
    NStmt*   ctor;         // constructor body (SK_FUNC shape)
    char     fields[16][32];
    bool     field_public[16];
    int      fieldc;
};

static NExpr* mk_expr(EK k) {
    NExpr* e=(NExpr*)nv_alloc(sizeof(NExpr));
    if(e) e->kind=k;
    return e;
}
static NStmt* mk_stmt(SK k) {
    NStmt* s=(NStmt*)nv_alloc(sizeof(NStmt));
    if(s) s->kind=k;
    return s;
}
static NStmt** mk_body(int n) {
    if(n<=0) return nullptr;
    return (NStmt**)nv_alloc(n*(int)sizeof(NStmt*));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Parser  — recursive descent
// ═════════════════════════════════════════════════════════════════════════════

struct Parser { Lexer* L; };

static NExpr* p_expr(Parser* P);
static NStmt* p_stmt(Parser* P);

static void p_body(Parser* P, NStmt**& out, int& outc) {
    NStmt* buf[128]; outc=0;
    while (!lx_is(P->L,T_SYM,"}") && !lx_is(P->L,T_EOF)) {
        NStmt* s=p_stmt(P);
        if (s && outc<128) buf[outc++]=s;
    }
    if (lx_is(P->L,T_SYM,"}")) lx_eat(P->L);
    out=mk_body(outc);
    if (out) k_memcpy(out,buf,outc*(int)sizeof(NStmt*));
}

static int p_args(Parser* P, NExpr** args, int max) {
    int c=0;
    if (lx_is(P->L,T_SYM,")")) return 0;
    while (c<max) {
        args[c++]=p_expr(P);
        if (!lx_is(P->L,T_SYM,",")) break;
        lx_eat(P->L);
    }
    return c;
}

// ── factor ───────────────────────────────────────────────────────────────────
static NExpr* p_factor(Parser* P) {
    Token t=P->L->cur;

    // Grouped expression
    if (lx_is(P->L,T_SYM,"(")) {
        lx_eat(P->L);
        NExpr* e=p_expr(P);
        lx_expect(P->L,T_SYM,")");
        return e;
    }
    // Unary  !  -  ~
    if (lx_is(P->L,T_OP,"!")||lx_is(P->L,T_OP,"-")||lx_is(P->L,T_OP,"~")) {
        lx_eat(P->L);
        NExpr* e=mk_expr(EK_UNI);
        if(e){ k_strncpy(e->op,t.lex,3); e->left=p_factor(P); }
        return e;
    }
    // Literals
    if (lx_is(P->L,T_KW,"true"))  { lx_eat(P->L); NExpr* e=mk_expr(EK_BOOL); if(e)e->ival=1; return e; }
    if (lx_is(P->L,T_KW,"false")) { lx_eat(P->L); NExpr* e=mk_expr(EK_BOOL); if(e)e->ival=0; return e; }
    if (lx_is(P->L,T_NUM))   { lx_eat(P->L); NExpr* e=mk_expr(EK_NUM); if(e)e->ival=t.inum; return e; }
    if (lx_is(P->L,T_FLOAT)) { lx_eat(P->L); NExpr* e=mk_expr(EK_FLT); if(e)e->fval=t.fnum; return e; }
    if (lx_is(P->L,T_STR))   { lx_eat(P->L); NExpr* e=mk_expr(EK_STR); if(e)k_strncpy(e->sval,t.lex,127); return e; }

    // new ClassName(args)
    if (lx_is(P->L,T_KW,"new")) {
        lx_eat(P->L);
        Token cls=lx_expect(P->L,T_IDENT);
        lx_expect(P->L,T_SYM,"(");
        NExpr* e=mk_expr(EK_NEW);
        if(e){ k_strncpy(e->sval,cls.lex,127); e->argc=p_args(P,e->args,8); }
        lx_expect(P->L,T_SYM,")");
        return e;
    }

    // self  or  self.prop  or  self.method(args)
    if (lx_is(P->L,T_KW,"self")) {
        lx_eat(P->L);
        if (lx_is(P->L,T_SYM,".")) {
            lx_eat(P->L);
            Token prop=lx_expect(P->L,T_IDENT);
            if (lx_is(P->L,T_SYM,"(")) {
                lx_eat(P->L);
                NExpr* e=mk_expr(EK_METH);
                if(e){ k_strncpy(e->sval,"self",127); k_strncpy(e->sval2,prop.lex,63); e->argc=p_args(P,e->args,8); }
                lx_expect(P->L,T_SYM,")");
                return e;
            }
            NExpr* e=mk_expr(EK_PROP);
            if(e){ k_strncpy(e->sval,"self",127); k_strncpy(e->sval2,prop.lex,63); }
            return e;
        }
        return mk_expr(EK_SELF);
    }

    // pin(var) used in expression context
    if (lx_is(P->L,T_KW,"pin")) {
        lx_eat(P->L); lx_expect(P->L,T_SYM,"(");
        Token v=lx_expect(P->L,T_IDENT);
        lx_expect(P->L,T_SYM,")");
        NExpr* e=mk_expr(EK_VAR);
        if(e) k_strncpy(e->sval,v.lex,127);
        return e;
    }

    // Identifier — var, call, array index, prop access, method call
    if (lx_is(P->L,T_IDENT)) {
        lx_eat(P->L);
        // func call: name(args)
        if (lx_is(P->L,T_SYM,"(")) {
            lx_eat(P->L);
            NExpr* e=mk_expr(EK_CALL);
            if(e){ k_strncpy(e->sval,t.lex,127); e->argc=p_args(P,e->args,8); }
            lx_expect(P->L,T_SYM,")");
            return e;
        }
        // array: name[expr]
        if (lx_is(P->L,T_SYM,"[")) {
            lx_eat(P->L);
            NExpr* idx=p_expr(P); lx_expect(P->L,T_SYM,"]");
            NExpr* e=mk_expr(EK_IDX);
            if(e){ k_strncpy(e->sval,t.lex,127); e->right=idx; }
            return e;
        }
        // prop / method: name.member  or  name.method(args)
        if (lx_is(P->L,T_SYM,".")) {
            lx_eat(P->L);
            Token prop=lx_expect(P->L,T_IDENT);
            if (lx_is(P->L,T_SYM,"(")) {
                lx_eat(P->L);
                NExpr* e=mk_expr(EK_METH);
                if(e){ k_strncpy(e->sval,t.lex,127); k_strncpy(e->sval2,prop.lex,63); e->argc=p_args(P,e->args,8); }
                lx_expect(P->L,T_SYM,")");
                return e;
            }
            NExpr* e=mk_expr(EK_PROP);
            if(e){ k_strncpy(e->sval,t.lex,127); k_strncpy(e->sval2,prop.lex,63); }
            return e;
        }
        // plain variable
        NExpr* e=mk_expr(EK_VAR);
        if(e) k_strncpy(e->sval,t.lex,127);
        return e;
    }
    return nullptr;
}

// Multiplicative
static NExpr* p_term(Parser* P) {
    NExpr* left=p_factor(P);
    while (lx_is(P->L,T_OP,"*")||lx_is(P->L,T_OP,"/")||lx_is(P->L,T_OP,"%")) {
        Token op=lx_eat(P->L);
        NExpr* e=mk_expr(EK_BIN);
        if(e){ k_strncpy(e->op,op.lex,3); e->left=left; e->right=p_factor(P); }
        left=e;
    }
    return left;
}
// Additive
static NExpr* p_add(Parser* P) {
    NExpr* left=p_term(P);
    while (lx_is(P->L,T_OP,"+")||lx_is(P->L,T_OP,"-")) {
        Token op=lx_eat(P->L);
        NExpr* e=mk_expr(EK_BIN);
        if(e){ k_strncpy(e->op,op.lex,3); e->left=left; e->right=p_term(P); }
        left=e;
    }
    return left;
}
// Shift
static NExpr* p_shift(Parser* P) {
    NExpr* left=p_add(P);
    while (lx_is(P->L,T_OP,"<<")||lx_is(P->L,T_OP,">>")) {
        Token op=lx_eat(P->L);
        NExpr* e=mk_expr(EK_BIN);
        if(e){ k_strncpy(e->op,op.lex,3); e->left=left; e->right=p_add(P); }
        left=e;
    }
    return left;
}
// Comparison + equality
static NExpr* p_cmp(Parser* P) {
    NExpr* left=p_shift(P);
    while (lx_is(P->L,T_OP,"<")||lx_is(P->L,T_OP,">")||
           lx_is(P->L,T_OP,"<=")||lx_is(P->L,T_OP,">=")||
           lx_is(P->L,T_OP,"==")||lx_is(P->L,T_OP,"!=")) {
        Token op=lx_eat(P->L);
        NExpr* e=mk_expr(EK_BIN);
        if(e){ k_strncpy(e->op,op.lex,3); e->left=left; e->right=p_shift(P); }
        left=e;
    }
    return left;
}
// Bitwise AND / XOR / OR
static NExpr* p_bitwise(Parser* P) {
    NExpr* left=p_cmp(P);
    while (lx_is(P->L,T_OP,"&")||lx_is(P->L,T_OP,"^")||lx_is(P->L,T_OP,"|")) {
        Token op=lx_eat(P->L);
        NExpr* e=mk_expr(EK_BIN);
        if(e){ k_strncpy(e->op,op.lex,3); e->left=left; e->right=p_cmp(P); }
        left=e;
    }
    return left;
}
// Logical AND / OR — top level
static NExpr* p_expr(Parser* P) {
    NExpr* left=p_bitwise(P);
    while (lx_is(P->L,T_OP,"&&")||lx_is(P->L,T_OP,"||")) {
        Token op=lx_eat(P->L);
        NExpr* e=mk_expr(EK_BIN);
        if(e){ k_strncpy(e->op,op.lex,3); e->left=left; e->right=p_bitwise(P); }
        left=e;
    }
    return left;
}

// ── Statement parser ──────────────────────────────────────────────────────────
static NStmt* p_stmt(Parser* P) {
    Token t=P->L->cur;

    // int / float / string declaration
    if (lx_is(P->L,T_KW,"int")||lx_is(P->L,T_KW,"float")||lx_is(P->L,T_KW,"string")) {
        lx_eat(P->L);
        Token name=lx_expect(P->L,T_IDENT);
        NStmt* s=mk_stmt(SK_DECL);
        if(s) {
            k_strncpy(s->name,name.lex,63);
            if (lx_is(P->L,T_OP,"=")) { lx_eat(P->L); s->expr=p_expr(P); }
        }
        lx_expect(P->L,T_SYM,";");
        return s;
    }
    // ClassName varName = new ClassName(args);  — typed object declaration
    // This is recognised when token is an IDENT followed by another IDENT
    // We detect this in the IDENT block below.

    // list declaration
    if (lx_is(P->L,T_KW,"list")) {
        lx_eat(P->L);
        Token name=lx_expect(P->L,T_IDENT);
        NStmt* s=mk_stmt(SK_LIST);
        if(s) k_strncpy(s->name,name.lex,63);
        lx_expect(P->L,T_SYM,";");
        return s;
    }
    // return
    if (lx_is(P->L,T_KW,"return")) {
        lx_eat(P->L);
        NStmt* s=mk_stmt(SK_RET);
        if(s && !lx_is(P->L,T_SYM,";")) s->expr=p_expr(P);
        lx_expect(P->L,T_SYM,";");
        return s;
    }
    // delete
    if (lx_is(P->L,T_KW,"delete")) {
        lx_eat(P->L);
        Token name=lx_expect(P->L,T_IDENT);
        NStmt* s=mk_stmt(SK_DEL);
        if(s) k_strncpy(s->name,name.lex,63);
        lx_expect(P->L,T_SYM,";");
        return s;
    }
    // if
    if (lx_is(P->L,T_KW,"if")) {
        lx_eat(P->L); lx_expect(P->L,T_SYM,"(");
        NExpr* cond=p_expr(P); lx_expect(P->L,T_SYM,")");
        lx_expect(P->L,T_SYM,"{");
        NStmt* s=mk_stmt(SK_IF);
        if(s) {
            s->cond=cond;
            p_body(P,s->body,s->bodyc);
            if (lx_is(P->L,T_KW,"else")) {
                lx_eat(P->L); lx_expect(P->L,T_SYM,"{");
                p_body(P,s->els,s->elsc);
            }
        }
        return s;
    }
    // while
    if (lx_is(P->L,T_KW,"while")) {
        lx_eat(P->L); lx_expect(P->L,T_SYM,"(");
        NExpr* cond=p_expr(P); lx_expect(P->L,T_SYM,")");
        lx_expect(P->L,T_SYM,"{");
        NStmt* s=mk_stmt(SK_WHILE);
        if(s){ s->cond=cond; p_body(P,s->body,s->bodyc); }
        return s;
    }
    // for  — NodeV style: for (int i = 0, i < 10, i = i + 1) { }
    if (lx_is(P->L,T_KW,"for")) {
        lx_eat(P->L); lx_expect(P->L,T_SYM,"(");
        if (lx_is(P->L,T_KW,"int")||lx_is(P->L,T_KW,"float")) lx_eat(P->L);
        Token ivar=lx_expect(P->L,T_IDENT);
        lx_expect(P->L,T_OP,"=");
        NExpr* initVal=p_expr(P);
        NStmt* initStmt=mk_stmt(SK_DECL);
        if(initStmt){ k_strncpy(initStmt->name,ivar.lex,63); initStmt->expr=initVal; }
        lx_expect(P->L,T_SYM,",");
        NExpr* cond=p_expr(P);
        lx_expect(P->L,T_SYM,",");
        Token uvar=lx_expect(P->L,T_IDENT);
        lx_expect(P->L,T_OP,"=");
        NExpr* updVal=p_expr(P);
        NStmt* updStmt=mk_stmt(SK_ASSIGN);
        if(updStmt){ k_strncpy(updStmt->name,uvar.lex,63); updStmt->expr=updVal; }
        lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,"{");
        NStmt* s=mk_stmt(SK_FOR);
        if(s){ s->cond=cond; s->finit=initStmt; s->fupdate=updStmt; p_body(P,s->body,s->bodyc); }
        return s;
    }
    // function
    if (lx_is(P->L,T_KW,"function")) {
        lx_eat(P->L);
        Token name=lx_expect(P->L,T_IDENT);
        lx_expect(P->L,T_SYM,"(");
        NStmt* s=mk_stmt(SK_FUNC);
        if(s) {
            k_strncpy(s->name,name.lex,63);
            while (!lx_is(P->L,T_SYM,")") && s->paramc<8) {
                Token p=lx_expect(P->L,T_IDENT);
                k_strncpy(s->params[s->paramc++],p.lex,31);
                if (lx_is(P->L,T_SYM,",")) lx_eat(P->L);
            }
            lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,"{");
            p_body(P,s->body,s->bodyc);
        }
        return s;
    }
    // pout
    if (lx_is(P->L,T_KW,"pout")) {
        lx_eat(P->L); lx_expect(P->L,T_SYM,"(");
        NStmt* s=mk_stmt(SK_PRINT);
        if(s) {
            while (!lx_is(P->L,T_SYM,")") && s->pargc<8) {
                s->pargs[s->pargc++]=p_expr(P);
                if (lx_is(P->L,T_SYM,",")) lx_eat(P->L);
            }
        }
        lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,";");
        return s;
    }
    // pin
    if (lx_is(P->L,T_KW,"pin")) {
        lx_eat(P->L); lx_expect(P->L,T_SYM,"(");
        Token v=lx_expect(P->L,T_IDENT);
        NStmt* s=mk_stmt(SK_INPUT);
        if(s) k_strncpy(s->name,v.lex,63);
        lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,";");
        return s;
    }
    // class
    if (lx_is(P->L,T_KW,"class")) {
        lx_eat(P->L);
        Token cname=lx_expect(P->L,T_IDENT);
        lx_expect(P->L,T_SYM,"{");
        NStmt* s=mk_stmt(SK_CLASS);
        if(!s) return nullptr;
        k_strncpy(s->name,cname.lex,63);
        bool is_pub=true; // default public
        while (!lx_is(P->L,T_SYM,"}") && !lx_is(P->L,T_EOF)) {
            // access modifier
            if (lx_is(P->L,T_KW,"public"))  { lx_eat(P->L); is_pub=true;
                if(lx_is(P->L,T_SYM,":")) lx_eat(P->L); continue; }
            if (lx_is(P->L,T_KW,"private")) { lx_eat(P->L); is_pub=false;
                if(lx_is(P->L,T_SYM,":")) lx_eat(P->L); continue; }
            // constructor
            if (lx_is(P->L,T_KW,"constructor")) {
                lx_eat(P->L); lx_expect(P->L,T_SYM,"(");
                NStmt* ctor=mk_stmt(SK_FUNC);
                if(ctor){
                    k_strncpy(ctor->name,"constructor",63);
                    while (!lx_is(P->L,T_SYM,")") && ctor->paramc<8) {
                        Token p=lx_expect(P->L,T_IDENT);
                        k_strncpy(ctor->params[ctor->paramc++],p.lex,31);
                        if(lx_is(P->L,T_SYM,",")) lx_eat(P->L);
                    }
                    lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,"{");
                    p_body(P,ctor->body,ctor->bodyc);
                    s->ctor=ctor;
                }
                continue;
            }
            // destructor — parse but ignore body (no heap to manage in arena)
            if (lx_is(P->L,T_KW,"destructor")) {
                lx_eat(P->L); lx_expect(P->L,T_SYM,"(");
                lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,"{");
                NStmt** dummy; int dc=0; p_body(P,dummy,dc);
                continue;
            }
            // method
            if (lx_is(P->L,T_KW,"function")) {
                lx_eat(P->L);
                Token mname=lx_expect(P->L,T_IDENT);
                lx_expect(P->L,T_SYM,"(");
                NStmt* mstmt=mk_stmt(SK_FUNC);
                if(mstmt && s->methodc<16) {
                    k_strncpy(mstmt->name,mname.lex,63);
                    while(!lx_is(P->L,T_SYM,")")&&mstmt->paramc<8){
                        Token p=lx_expect(P->L,T_IDENT);
                        k_strncpy(mstmt->params[mstmt->paramc++],p.lex,31);
                        if(lx_is(P->L,T_SYM,",")) lx_eat(P->L);
                    }
                    lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,"{");
                    p_body(P,mstmt->body,mstmt->bodyc);
                    s->methods[s->methodc].decl=mstmt;
                    k_strncpy(s->methods[s->methodc].name,mname.lex,31);
                    s->methods[s->methodc].is_public=is_pub;
                    s->methodc++;
                }
                continue;
            }
            // field: int/float/string name;
            if (lx_is(P->L,T_KW,"int")||lx_is(P->L,T_KW,"float")||lx_is(P->L,T_KW,"string")) {
                lx_eat(P->L);
                if (lx_is(P->L,T_IDENT) && s->fieldc<16) {
                    Token fn=lx_eat(P->L);
                    k_strncpy(s->fields[s->fieldc],fn.lex,31);
                    s->field_public[s->fieldc]=is_pub;
                    s->fieldc++;
                }
                lx_expect(P->L,T_SYM,";");
                continue;
            }
            // skip unknown
            if (!lx_is(P->L,T_EOF)) lx_eat(P->L);
        }
        if (lx_is(P->L,T_SYM,"}")) lx_eat(P->L);
        return s;
    }
    // self.prop = expr;  (statement form used inside methods)
    if (lx_is(P->L,T_KW,"self")) {
        lx_eat(P->L); lx_expect(P->L,T_SYM,".");
        Token prop=lx_expect(P->L,T_IDENT);
        lx_expect(P->L,T_OP,"=");
        NStmt* s=mk_stmt(SK_PASGN);
        if(s){ k_strncpy(s->name,"self",63); k_strncpy(s->name2,prop.lex,63); s->expr=p_expr(P); }
        lx_expect(P->L,T_SYM,";");
        return s;
    }

    // Identifier-led statements
    if (lx_is(P->L,T_IDENT)) {
        Token id=lx_eat(P->L);

        // Typed object declaration: ClassName varName = new ... ;
        // Detected when two consecutive IDENTs appear (second is var name)
        if (lx_is(P->L,T_IDENT)) {
            Token varname=lx_eat(P->L);
            NStmt* s=mk_stmt(SK_DECL);
            if(s){ k_strncpy(s->name,varname.lex,63); }
            if (lx_is(P->L,T_OP,"=")) { lx_eat(P->L); if(s) s->expr=p_expr(P); }
            lx_expect(P->L,T_SYM,";");
            return s;
        }

        // obj.prop = expr;  or  obj.method(args);
        if (lx_is(P->L,T_SYM,".")) {
            lx_eat(P->L);
            Token prop=lx_expect(P->L,T_IDENT);
            if (lx_is(P->L,T_OP,"=")) {
                lx_eat(P->L);
                NStmt* s=mk_stmt(SK_PASGN);
                if(s){ k_strncpy(s->name,id.lex,63); k_strncpy(s->name2,prop.lex,63); s->expr=p_expr(P); }
                lx_expect(P->L,T_SYM,";"); return s;
            }
            if (lx_is(P->L,T_SYM,"(")) {
                lx_eat(P->L);
                NExpr* e=mk_expr(EK_METH);
                if(e){ k_strncpy(e->sval,id.lex,127); k_strncpy(e->sval2,prop.lex,63); e->argc=p_args(P,e->args,8); }
                lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,";");
                NStmt* s=mk_stmt(SK_EXPR); if(s) s->expr=e; return s;
            }
        }
        // arr[idx] = expr;
        if (lx_is(P->L,T_SYM,"[")) {
            lx_eat(P->L); NExpr* idx=p_expr(P); lx_expect(P->L,T_SYM,"]");
            lx_expect(P->L,T_OP,"=");
            NStmt* s=mk_stmt(SK_IASGN);
            if(s){ k_strncpy(s->name,id.lex,63); s->idx=idx; s->expr=p_expr(P); }
            lx_expect(P->L,T_SYM,";"); return s;
        }
        // var = expr;
        if (lx_is(P->L,T_OP,"=")) {
            lx_eat(P->L);
            NStmt* s=mk_stmt(SK_ASSIGN);
            if(s){ k_strncpy(s->name,id.lex,63); s->expr=p_expr(P); }
            lx_expect(P->L,T_SYM,";"); return s;
        }
        // func call as statement: f(args);
        if (lx_is(P->L,T_SYM,"(")) {
            lx_eat(P->L);
            NExpr* e=mk_expr(EK_CALL);
            if(e){ k_strncpy(e->sval,id.lex,127); e->argc=p_args(P,e->args,8); }
            lx_expect(P->L,T_SYM,")"); lx_expect(P->L,T_SYM,";");
            NStmt* s=mk_stmt(SK_EXPR); if(s) s->expr=e; return s;
        }
        // bare identifier — skip
        if (lx_is(P->L,T_SYM,";")) lx_eat(P->L);
        return nullptr;
    }

    // Skip unknown tokens
    if (!lx_is(P->L,T_EOF)) lx_eat(P->L);
    return nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Value  (runtime value type)
// ═════════════════════════════════════════════════════════════════════════════

enum NVType { NVT_INT=0, NVT_FLOAT, NVT_STRING, NVT_OBJ };
struct NVValue {
    NVType type;
    int    i;       // NVT_INT, NVT_OBJ (object id)
    double f;       // NVT_FLOAT
    char   s[128];  // NVT_STRING (up from 64)
};

static NVValue nv_int(int v)         { NVValue r; r.type=NVT_INT;   r.i=v; r.f=0; r.s[0]='\0'; return r; }
static NVValue nv_float(double v)    { NVValue r; r.type=NVT_FLOAT; r.f=v; r.i=0; r.s[0]='\0'; return r; }
static NVValue nv_str(const char* v) { NVValue r; r.type=NVT_STRING;r.i=0; r.f=0; k_strncpy(r.s,v,127); return r; }
static NVValue nv_obj(int id)        { NVValue r; r.type=NVT_OBJ;   r.i=id;r.f=0; r.s[0]='\0'; return r; }

static bool nv_truthy(NVValue v) {
    if (v.type==NVT_INT||v.type==NVT_OBJ) return v.i!=0;
    if (v.type==NVT_FLOAT)  return v.f!=0.0;
    if (v.type==NVT_STRING) return v.s[0]!='\0';
    return false;
}
static void nv_tostr(NVValue v, char* buf, int sz) {
    if (v.type==NVT_STRING) { k_strncpy(buf,v.s,sz-1); buf[sz-1]='\0'; return; }
    if (v.type==NVT_INT||v.type==NVT_OBJ) { k_itoa(v.i,buf,10); return; }
    // float
    int whole=(int)v.f;
    int frac=(int)((v.f-whole)*1000);
    if(frac<0) frac=-frac;
    char tmp[32]; k_itoa(whole,tmp,10);
    k_strcpy(buf,tmp); k_strcat(buf,".");
    char ftmp[8]; k_itoa(frac,ftmp,10); k_strcat(buf,ftmp);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Environment  — scope chain, function table, object heap
// ═════════════════════════════════════════════════════════════════════════════

struct NVVar   { char name[64]; NVValue val; bool used; };
struct NVScope {
    NVVar    vars[32];   // up from 24
    int      vc;
    NVScope* parent;
};
struct NVFunc  { char name[64]; NStmt* decl; bool used; };
struct NVList  { char name[64]; NVValue items[64]; int count; bool used; };
struct NVClass { char name[64]; NStmt* decl; bool used; };

// Object instance on the object heap
struct NVObjField { char name[64]; NVValue val; bool used; };
struct NVObject {
    int        id;
    char       className[64];
    NVObjField fields[32];
    int        fieldc;
    bool       used;
};

static NVScope* nv_new_scope(NVScope* parent) {
    NVScope* s=(NVScope*)nv_alloc(sizeof(NVScope));
    if(s){ s->vc=0; s->parent=parent; }
    return s;
}
static NVValue* nv_find_var(NVScope* sc, const char* name) {
    for (NVScope* c=sc;c;c=c->parent)
        for (int i=0;i<c->vc;i++)
            if (c->vars[i].used && k_strcmp(c->vars[i].name,name)==0)
                return &c->vars[i].val;
    return nullptr;
}
static void nv_set_var(NVScope* sc, const char* name, NVValue v) {
    for (NVScope* c=sc;c;c=c->parent)
        for (int i=0;i<c->vc;i++)
            if (c->vars[i].used && k_strcmp(c->vars[i].name,name)==0)
                { c->vars[i].val=v; return; }
    if (sc && sc->vc<32) {
        k_strncpy(sc->vars[sc->vc].name,name,63);
        sc->vars[sc->vc].val=v; sc->vars[sc->vc].used=true; sc->vc++;
    }
}

// Global tables (reset per execution)
static NVFunc  nv_funcs[32];
static NVList  nv_lists[16];
static NVClass nv_classes[16];
static NVObject nv_heap[32];
static int     nv_next_obj=1;

static bool    nv_ret=false;
static NVValue nv_ret_val;

// Input queue for pin()
static const int NV_MAX_INPUTS_INTERNAL = 16;
static char nv_input_queue[NV_MAX_INPUTS_INTERNAL][64];
static int  nv_input_count=0;
static int  nv_input_idx=0;

// "self" context when executing inside a method
static int nv_self_id=0; // object id, 0 = none

static NVFunc* nv_find_func(const char* name) {
    for (int i=0;i<32;i++)
        if (nv_funcs[i].used && k_strcmp(nv_funcs[i].name,name)==0) return &nv_funcs[i];
    return nullptr;
}
static NVList* nv_find_list(const char* name) {
    for (int i=0;i<16;i++)
        if (nv_lists[i].used && k_strcmp(nv_lists[i].name,name)==0) return &nv_lists[i];
    return nullptr;
}
static NVClass* nv_find_class(const char* name) {
    for (int i=0;i<16;i++)
        if (nv_classes[i].used && k_strcmp(nv_classes[i].name,name)==0) return &nv_classes[i];
    return nullptr;
}
static NVObject* nv_find_obj(int id) {
    for (int i=0;i<32;i++)
        if (nv_heap[i].used && nv_heap[i].id==id) return &nv_heap[i];
    return nullptr;
}
static NVValue* nv_obj_field(NVObject* obj, const char* name) {
    for (int i=0;i<obj->fieldc;i++)
        if (obj->fields[i].used && k_strcmp(obj->fields[i].name,name)==0)
            return &obj->fields[i].val;
    return nullptr;
}
static void nv_obj_set_field(NVObject* obj, const char* name, NVValue v) {
    for (int i=0;i<obj->fieldc;i++)
        if (obj->fields[i].used && k_strcmp(obj->fields[i].name,name)==0)
            { obj->fields[i].val=v; return; }
    if (obj->fieldc<32) {
        k_strncpy(obj->fields[obj->fieldc].name,name,63);
        obj->fields[obj->fieldc].val=v;
        obj->fields[obj->fieldc].used=true;
        obj->fieldc++;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Interpreter  — eval / exec  (no dynamic_cast, uses tagged-union dispatch)
// ═════════════════════════════════════════════════════════════════════════════

static NVValue nv_eval(NExpr* e, NVScope* sc);
static void    nv_exec(NStmt* s, NVScope* sc);

static NVValue nv_eval(NExpr* e, NVScope* sc) {
    if (!e) return nv_int(0);
    if (++nv_iters>NV_MAX_ITERS) return nv_int(0);

    switch (e->kind) {
    case EK_NUM:  return nv_int(e->ival);
    case EK_FLT:  return nv_float(e->fval);
    case EK_STR:  return nv_str(e->sval);
    case EK_BOOL: return nv_int(e->ival);
    case EK_SELF: return nv_obj(nv_self_id);

    case EK_VAR: {
        NVValue* v=nv_find_var(sc,e->sval);
        return v ? *v : nv_int(0);
    }
    case EK_IDX: {
        // List indexing
        NVList* lst=nv_find_list(e->sval);
        if (lst) {
            NVValue idx=nv_eval(e->right,sc);
            int i=idx.type==NVT_INT?idx.i:(int)idx.f;
            if (i>=0&&i<lst->count) return lst->items[i];
            return nv_int(0);
        }
        // String indexing
        NVValue* sv=nv_find_var(sc,e->sval);
        if (sv && sv->type==NVT_STRING) {
            NVValue idx=nv_eval(e->right,sc);
            int i=idx.type==NVT_INT?idx.i:(int)idx.f;
            if (i>=0&&i<(int)k_strlen(sv->s)) {
                char ch[2]={sv->s[i],'\0'}; return nv_str(ch);
            }
        }
        return nv_int(0);
    }
    case EK_UNI: {
        NVValue v=nv_eval(e->left,sc);
        if (k_strcmp(e->op,"-")==0) return v.type==NVT_FLOAT?nv_float(-v.f):nv_int(-v.i);
        if (k_strcmp(e->op,"!")==0) return nv_int(!nv_truthy(v));
        if (k_strcmp(e->op,"~")==0&&v.type==NVT_INT) return nv_int(~v.i);
        return v;
    }
    case EK_BIN: {
        NVValue l=nv_eval(e->left,sc);
        NVValue r=nv_eval(e->right,sc);
        const char* op=e->op;

        // String operations
        if (l.type==NVT_STRING||r.type==NVT_STRING) {
            if (k_strcmp(op,"+")==0) {
                char a[128],b[128],res[256];
                nv_tostr(l,a,128); nv_tostr(r,b,128);
                k_strncpy(res,a,127); k_strncat(res,b,255);
                return nv_str(res);
            }
            if (k_strcmp(op,"==")==0) return nv_int(k_strcmp(l.s,r.s)==0);
            if (k_strcmp(op,"!=")==0) return nv_int(k_strcmp(l.s,r.s)!=0);
            if (k_strcmp(op,"<")==0)  return nv_int(k_strcmp(l.s,r.s)<0);
            if (k_strcmp(op,">")==0)  return nv_int(k_strcmp(l.s,r.s)>0);
            if (k_strcmp(op,"<=")==0) return nv_int(k_strcmp(l.s,r.s)<=0);
            if (k_strcmp(op,">=")==0) return nv_int(k_strcmp(l.s,r.s)>=0);
        }

        // Numeric
        bool useF=(l.type==NVT_FLOAT||r.type==NVT_FLOAT);
        double lf=l.type==NVT_INT?(double)l.i:l.f;
        double rf=r.type==NVT_INT?(double)r.i:r.f;
        int    li=l.type==NVT_INT?l.i:(int)l.f;
        int    ri=r.type==NVT_INT?r.i:(int)r.f;

        if (k_strcmp(op,"+")==0)  return useF?nv_float(lf+rf):nv_int(li+ri);
        if (k_strcmp(op,"-")==0)  return useF?nv_float(lf-rf):nv_int(li-ri);
        if (k_strcmp(op,"*")==0)  return useF?nv_float(lf*rf):nv_int(li*ri);
        if (k_strcmp(op,"/")==0)  { if(rf==0){shell_println("[NodeV] Div/0");return nv_int(0);}
                                    return nv_float(lf/rf); }
        if (k_strcmp(op,"%")==0)  { if(ri==0){shell_println("[NodeV] Mod/0");return nv_int(0);}
                                    return nv_int(li%ri); }
        if (k_strcmp(op,"<")==0)  return useF?nv_int(lf<rf):nv_int(li<ri);
        if (k_strcmp(op,">")==0)  return useF?nv_int(lf>rf):nv_int(li>ri);
        if (k_strcmp(op,"<=")==0) return useF?nv_int(lf<=rf):nv_int(li<=ri);
        if (k_strcmp(op,">=")==0) return useF?nv_int(lf>=rf):nv_int(li>=ri);
        if (k_strcmp(op,"==")==0) return useF?nv_int(lf==rf):nv_int(li==ri);
        if (k_strcmp(op,"!=")==0) return useF?nv_int(lf!=rf):nv_int(li!=ri);
        if (k_strcmp(op,"&&")==0) return nv_int(nv_truthy(l)&&nv_truthy(r));
        if (k_strcmp(op,"||")==0) return nv_int(nv_truthy(l)||nv_truthy(r));
        if (k_strcmp(op,"&")==0)  return nv_int(li&ri);
        if (k_strcmp(op,"|")==0)  return nv_int(li|ri);
        if (k_strcmp(op,"^")==0)  return nv_int(li^ri);
        if (k_strcmp(op,"<<")==0) return nv_int(li<<ri);
        if (k_strcmp(op,">>")==0) return nv_int(li>>ri);
        return nv_int(0);
    }
    case EK_CALL: {
        NVFunc* fn=nv_find_func(e->sval);
        if (!fn) {
            shell_print("[NodeV] Undefined function '");
            shell_print(e->sval); shell_println("'");
            return nv_int(0);
        }
        NStmt* fd=fn->decl;
        NVValue av[8]; int ac=e->argc<8?e->argc:8;
        for (int i=0;i<ac;i++) av[i]=nv_eval(e->args[i],sc);
        NVScope* fsc=nv_new_scope(nullptr); // isolated scope
        if (!fsc) return nv_int(0);
        int pc=fd->paramc<ac?fd->paramc:ac;
        for (int i=0;i<pc;i++) nv_set_var(fsc,fd->params[i],av[i]);
        bool prev_ret=nv_ret; nv_ret=false;
        for (int i=0;i<fd->bodyc&&!nv_ret;i++) nv_exec(fd->body[i],fsc);
        NVValue ret=nv_ret?nv_ret_val:nv_int(0);
        nv_ret=prev_ret;
        return ret;
    }
    case EK_NEW: {
        // Instantiate a class
        NVClass* cls=nv_find_class(e->sval);
        if (!cls) {
            shell_print("[NodeV] Undefined class '");
            shell_print(e->sval); shell_println("'");
            return nv_int(0);
        }
        // Find a free object slot
        NVObject* obj=nullptr;
        for (int i=0;i<32;i++) {
            if (!nv_heap[i].used) { obj=&nv_heap[i]; break; }
        }
        if (!obj) { shell_println("[NodeV] Object heap full"); return nv_int(0); }
        k_memset(obj,0,sizeof(NVObject));
        obj->id=nv_next_obj++;
        obj->used=true;
        k_strncpy(obj->className,e->sval,63);

        // Initialise declared fields to 0/"" 
        NStmt* cd=cls->decl;
        for (int i=0;i<cd->fieldc;i++) {
            NVValue zero=nv_int(0);
            nv_obj_set_field(obj,cd->fields[i],zero);
        }

        // Call constructor if present
        if (cd->ctor) {
            NVValue av[8]; int ac=e->argc<8?e->argc:8;
            for (int i=0;i<ac;i++) av[i]=nv_eval(e->args[i],sc);
            NVScope* fsc=nv_new_scope(nullptr);
            if (fsc) {
                int pc=cd->ctor->paramc<ac?cd->ctor->paramc:ac;
                for (int i=0;i<pc;i++) nv_set_var(fsc,cd->ctor->params[i],av[i]);
                int prev_self=nv_self_id; nv_self_id=obj->id;
                bool prev_ret=nv_ret; nv_ret=false;
                for (int i=0;i<cd->ctor->bodyc&&!nv_ret;i++) nv_exec(cd->ctor->body[i],fsc);
                nv_ret=prev_ret; nv_self_id=prev_self;
            }
        }
        return nv_obj(obj->id);
    }
    case EK_PROP: {
        // obj.field  or  self.field
        int target_id=0;
        if (k_strcmp(e->sval,"self")==0) {
            target_id=nv_self_id;
        } else {
            NVValue* v=nv_find_var(sc,e->sval);
            if (v && v->type==NVT_OBJ) target_id=v->i;
        }
        if (!target_id) return nv_int(0);
        NVObject* obj=nv_find_obj(target_id);
        if (!obj) return nv_int(0);
        NVValue* fv=nv_obj_field(obj,e->sval2);
        return fv ? *fv : nv_int(0);
    }
    case EK_METH: {
        // obj.method(args)  or  self.method(args)
        int target_id=0;
        if (k_strcmp(e->sval,"self")==0) {
            target_id=nv_self_id;
        } else {
            NVValue* v=nv_find_var(sc,e->sval);
            if (v && v->type==NVT_OBJ) target_id=v->i;
        }
        if (!target_id) return nv_int(0);
        NVObject* obj=nv_find_obj(target_id);
        if (!obj) return nv_int(0);
        // Find method in class
        NVClass* cls=nv_find_class(obj->className);
        if (!cls) return nv_int(0);
        NStmt* mstmt=nullptr;
        for (int i=0;i<cls->decl->methodc;i++)
            if (k_strcmp(cls->decl->methods[i].name,e->sval2)==0)
                { mstmt=cls->decl->methods[i].decl; break; }
        if (!mstmt) {
            shell_print("[NodeV] Undefined method '");
            shell_print(e->sval2); shell_println("'");
            return nv_int(0);
        }
        // Evaluate args
        NVValue av[8]; int ac=e->argc<8?e->argc:8;
        for (int i=0;i<ac;i++) av[i]=nv_eval(e->args[i],sc);
        NVScope* fsc=nv_new_scope(nullptr);
        if (!fsc) return nv_int(0);
        int pc=mstmt->paramc<ac?mstmt->paramc:ac;
        for (int i=0;i<pc;i++) nv_set_var(fsc,mstmt->params[i],av[i]);
        int prev_self=nv_self_id; nv_self_id=target_id;
        bool prev_ret=nv_ret; nv_ret=false;
        for (int i=0;i<mstmt->bodyc&&!nv_ret;i++) nv_exec(mstmt->body[i],fsc);
        NVValue ret=nv_ret?nv_ret_val:nv_int(0);
        nv_ret=prev_ret; nv_self_id=prev_self;
        return ret;
    }
    }
    return nv_int(0);
}

static void nv_exec(NStmt* s, NVScope* sc) {
    if (!s||nv_ret) return;
    if (++nv_iters>NV_MAX_ITERS) { nv_ret=true; nv_ret_val=nv_int(-1); return; }

    switch (s->kind) {
    case SK_DECL:
        nv_set_var(sc,s->name,s->expr?nv_eval(s->expr,sc):nv_int(0));
        break;
    case SK_LIST: {
        for (int i=0;i<16;i++) if (!nv_lists[i].used) {
            k_strncpy(nv_lists[i].name,s->name,63);
            nv_lists[i].count=0; nv_lists[i].used=true; break;
        }
        break;
    }
    case SK_ASSIGN:
        nv_set_var(sc,s->name,nv_eval(s->expr,sc));
        break;
    case SK_IASGN: {
        NVList* lst=nv_find_list(s->name);
        if (lst) {
            NVValue idx=nv_eval(s->idx,sc);
            int i=idx.type==NVT_INT?idx.i:(int)idx.f;
            if (i>=0&&i<64) {
                if (i>=lst->count) lst->count=i+1;
                lst->items[i]=nv_eval(s->expr,sc);
            }
        }
        break;
    }
    case SK_PASGN: {
        // self.prop = val  or  obj.prop = val
        int target_id=0;
        if (k_strcmp(s->name,"self")==0) {
            target_id=nv_self_id;
        } else {
            NVValue* v=nv_find_var(sc,s->name);
            if (v && v->type==NVT_OBJ) target_id=v->i;
        }
        if (target_id) {
            NVObject* obj=nv_find_obj(target_id);
            if (obj) nv_obj_set_field(obj,s->name2,nv_eval(s->expr,sc));
        }
        break;
    }
    case SK_RET:
        nv_ret_val=s->expr?nv_eval(s->expr,sc):nv_int(0);
        nv_ret=true;
        break;
    case SK_IF: {
        NVScope* child=nv_new_scope(sc);
        if (!child) break;
        if (nv_truthy(nv_eval(s->cond,sc))) {
            for (int i=0;i<s->bodyc&&!nv_ret;i++) nv_exec(s->body[i],child);
        } else {
            for (int i=0;i<s->elsc&&!nv_ret;i++) nv_exec(s->els[i],child);
        }
        break;
    }
    case SK_WHILE: {
        while (!nv_ret&&nv_truthy(nv_eval(s->cond,sc))) {
            NVScope* child=nv_new_scope(sc);
            if (!child) break;
            for (int i=0;i<s->bodyc&&!nv_ret;i++) nv_exec(s->body[i],child);
        }
        break;
    }
    case SK_FOR: {
        NVScope* fsc=nv_new_scope(sc);
        if (!fsc) break;
        nv_exec(s->finit,fsc);
        while (!nv_ret&&nv_truthy(nv_eval(s->cond,fsc))) {
            NVScope* child=nv_new_scope(fsc);
            if (!child) break;
            for (int i=0;i<s->bodyc&&!nv_ret;i++) nv_exec(s->body[i],child);
            nv_exec(s->fupdate,fsc);
        }
        break;
    }
    case SK_FUNC: {
        for (int i=0;i<32;i++) if (!nv_funcs[i].used) {
            k_strncpy(nv_funcs[i].name,s->name,63);
            nv_funcs[i].decl=s; nv_funcs[i].used=true; break;
        }
        break;
    }
    case SK_CLASS: {
        for (int i=0;i<16;i++) if (!nv_classes[i].used) {
            k_strncpy(nv_classes[i].name,s->name,63);
            nv_classes[i].decl=s; nv_classes[i].used=true; break;
        }
        break;
    }
    case SK_DEL: {
        NVValue* v=nv_find_var(sc,s->name);
        if (v && v->type==NVT_OBJ) {
            NVObject* obj=nv_find_obj(v->i);
            if (obj) { obj->used=false; k_memset(obj,0,sizeof(NVObject)); }
        }
        break;
    }
    case SK_PRINT: {
        for (int i=0;i<s->pargc;i++) {
            NVValue v=nv_eval(s->pargs[i],sc);
            char buf[256]; nv_tostr(v,buf,256);
            shell_print(buf);
        }
        break;
    }
    case SK_INPUT: {
        char ibuf[64]; ibuf[0]='\0';
        if (nv_input_idx<nv_input_count) {
            k_strncpy(ibuf,nv_input_queue[nv_input_idx],63);
            ibuf[63]='\0';
            nv_input_idx++;
        } else if (shell_gui_output_active()) {
            shell_print("[NodeV] pin() exhausted inputs for '");
            shell_print(s->name); shell_println("'");
        } else {
            extern void keyboard_readline(char*,int);
            keyboard_readline(ibuf,64);
        }

        // ── Auto-detect numeric type ──────────────────────────────────────
        // If the string looks like an integer or float, store it as the
        // correct numeric type so arithmetic works correctly after pin().
        // e.g. pin(x) reads "3" → NVT_INT(3), not NVT_STRING("3"),
        // so x + y gives 5 rather than "32" (string concatenation).
        NVValue input_val;
        bool is_num  = (ibuf[0] != '\0');
        bool has_dot = false;
        int  nstart  = (ibuf[0]=='-') ? 1 : 0;
        if (!ibuf[nstart]) is_num = false;   // just "-" is not a number
        for (int j = nstart; ibuf[j] && is_num; j++) {
            if (ibuf[j] == '.') {
                if (has_dot) is_num = false;  // two dots → not a number
                else has_dot = true;
            } else if (ibuf[j] < '0' || ibuf[j] > '9') {
                is_num = false;
            }
        }
        if (is_num && has_dot) {
            // Parse float manually (no strtod in freestanding)
            double val = 0.0, frac = 0.1;
            bool   inF = false;
            int    sign = (ibuf[0]=='-') ? -1 : 1;
            for (int j = nstart; ibuf[j]; j++) {
                if (ibuf[j] == '.') { inF = true; continue; }
                int d = ibuf[j] - '0';
                if (!inF) val = val * 10.0 + d;
                else      { val += d * frac; frac *= 0.1; }
            }
            input_val = nv_float(sign * val);
        } else if (is_num) {
            input_val = nv_int(k_atoi(ibuf));
        } else {
            input_val = nv_str(ibuf);
        }
        nv_set_var(sc, s->name, input_val);
        break;
    }
    case SK_EXPR:
        nv_eval(s->expr,sc);
        break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  $import resolver  — expands $import "file.nod" directives in-place
//  Uses a fixed 4 KB expansion buffer; nested imports supported up to depth 4
// ═════════════════════════════════════════════════════════════════════════════

static const int NV_SRC_MAX = VFS_MAX_FILESIZE * 4; // 16 KB combined source
static char nv_src_buf[NV_SRC_MAX];

static void nv_resolve_imports(const char* src, char* out, int outsz, int depth) {
    if (depth>4) { k_strncpy(out,src,outsz-1); return; }
    int oi=0;
    const char* p=src;
    while (*p && oi<outsz-1) {
        // Check for $import "filename"
        if (p[0]=='$' && k_strncmp(p+1,"import",6)==0) {
            p+=7;
            while (*p==' '||*p=='\t') p++;
            if (*p=='"') {
                p++;
                char fname[VFS_MAX_PATH]; int fi=0;
                while (*p && *p!='"' && fi<VFS_MAX_PATH-1) fname[fi++]=*p++;
                fname[fi]='\0';
                if (*p=='"') p++;
                // Skip to next line
                while (*p && *p!='\n') p++;
                // Load imported file
                static char imp_buf[VFS_MAX_FILESIZE];
                if (vfs_read(fname,imp_buf,VFS_MAX_FILESIZE)>=0) {
                    static char exp_buf[VFS_MAX_FILESIZE*2];
                    nv_resolve_imports(imp_buf,exp_buf,VFS_MAX_FILESIZE*2,depth+1);
                    int elen=(int)k_strlen(exp_buf);
                    if (oi+elen<outsz-1) { k_memcpy(out+oi,exp_buf,elen); oi+=elen; }
                }
                continue;
            }
        }
        out[oi++]=*p++;
    }
    out[oi]='\0';
}

// ═════════════════════════════════════════════════════════════════════════════
//  Public API
// ═════════════════════════════════════════════════════════════════════════════

NodeVResult nodev_exec(const char* source) {
    // Reset arena and all state
    nv_top=0; nv_iters=0; nv_ret=false; nv_self_id=0; nv_next_obj=1;
    k_memset(nv_funcs,0,sizeof(nv_funcs));
    k_memset(nv_lists,0,sizeof(nv_lists));
    k_memset(nv_classes,0,sizeof(nv_classes));
    k_memset(nv_heap,0,sizeof(nv_heap));
    nv_input_idx=0;

    Lexer lx; lx_init(&lx,source);
    Parser P; P.L=&lx;

    NVScope* global=nv_new_scope(nullptr);
    if (!global) return NV_ERROR;

    // Collect all statements
    NStmt* stmts[256]; int sc=0;
    while (!lx_is(P.L,T_EOF) && sc<256) {
        NStmt* st=p_stmt(&P);
        if (!st) continue;
        stmts[sc++]=st;
        // Pass 1 hoist: functions and classes
        if (st->kind==SK_FUNC) {
            for (int i=0;i<32;i++) if (!nv_funcs[i].used) {
                k_strncpy(nv_funcs[i].name,st->name,63);
                nv_funcs[i].decl=st; nv_funcs[i].used=true; break;
            }
        } else if (st->kind==SK_CLASS) {
            for (int i=0;i<16;i++) if (!nv_classes[i].used) {
                k_strncpy(nv_classes[i].name,st->name,63);
                nv_classes[i].decl=st; nv_classes[i].used=true; break;
            }
        }
    }

    // Pass 2: execute non-decl statements
    for (int i=0;i<sc&&!nv_ret;i++) {
        if (stmts[i]->kind==SK_FUNC||stmts[i]->kind==SK_CLASS) continue;
        nv_exec(stmts[i],global);
    }

    return NV_OK;
}

NodeVResult nodev_run_file(const char* filename) {
    static char raw[VFS_MAX_FILESIZE];
    if (vfs_read(filename,raw,VFS_MAX_FILESIZE)<0) {
        shell_print("[NodeV] File not found: "); shell_println(filename);
        return NV_ERROR;
    }
    // Resolve $import directives
    nv_resolve_imports(raw,nv_src_buf,NV_SRC_MAX,0);
    return nodev_exec(nv_src_buf);
}

void nodev_set_inputs(const char inputs[][64], int count) {
    nv_input_count=(count<NV_MAX_INPUTS_INTERNAL)?count:NV_MAX_INPUTS_INTERNAL;
    nv_input_idx=0;
    for (int i=0;i<nv_input_count;i++) {
        k_strncpy(nv_input_queue[i],inputs[i],63);
        nv_input_queue[i][63]='\0';
    }
}