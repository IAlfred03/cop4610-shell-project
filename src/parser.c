// =============================
// File: src/parser.c
// =============================
#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------- small utils ----------
static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { perror("malloc"); exit(1); }
    return p;
}
static char *xstrdup(const char *s) {
    char *p = strdup(s);
    if (!p) { perror("strdup"); exit(1); }
    return p;
}

// ---------- lexer ----------
typedef struct { const char *s; size_t i; } lex_t;

static int l_peekc(lex_t *L) { return L->s[L->i]; }
static int l_getc (lex_t *L) { return L->s[L->i] ? L->s[L->i++] : '\0'; }
static void l_skip_ws(lex_t *L) {
    while (isspace((unsigned char)l_peekc(L))) L->i++;
}

static token_t tok_make(token_kind_t k, char *lex) {
    token_t t; t.kind = k; t.lexeme = lex; return t;
}

static token_t lex_word(lex_t *L) {
    char *buf = NULL; size_t cap = 0, len = 0;
#define PUT(ch) do{ if(len+1>=cap){cap=cap?cap*2:32; buf=realloc(buf,cap);} buf[len++]=(char)(ch);}while(0)

    for (;;) {
        int c = l_peekc(L);
        if (c == '\0' || isspace((unsigned char)c) || c=='<' || c=='>' || c=='|' || c=='&') break;

        if (c == '\\') {           // escape next char outside quotes
            l_getc(L);
            int n = l_getc(L);
            if (n == '\0') break;
            PUT(n);
            continue;
        }
        if (c == '"' || c == '\'') { // quoted run
            int quote = l_getc(L);
            for (;;) {
                int q = l_getc(L);
                if (q == '\0') { free(buf); return tok_make(TK_ERR, NULL); } // unclosed quote
                if (q == quote) break;
                if (q == '\\') { // allow escapes inside quotes as well
                    int n = l_getc(L);
                    if (n == '\0') { free(buf); return tok_make(TK_ERR, NULL); }
                    PUT(n);
                } else {
                    PUT(q);
                }
            }
            continue;
        }
        PUT(l_getc(L));
    }
    PUT('\0');
    return tok_make(TK_WORD, buf ? buf : xstrdup(""));
}

static token_t lex_next(lex_t *L) {
    l_skip_ws(L);
    int c = l_peekc(L);
    if (c == '\0') return tok_make(TK_EOL, NULL);
    if (c == '<') { l_getc(L); return tok_make(TK_LT,  NULL); }
    if (c == '>') {
        l_getc(L);
        if (l_peekc(L) == '>') { l_getc(L); return tok_make(TK_DGT, NULL); }
        return tok_make(TK_GT, NULL);
    }
    if (c == '|') { l_getc(L); return tok_make(TK_BAR, NULL); }
    if (c == '&') { l_getc(L); return tok_make(TK_AMP, NULL); }
    return lex_word(L);
}

// ---------- one-token lookahead parser wrapper ----------
typedef struct {
    lex_t L;
    token_t la;
    int have_la; // 0 = empty, 1 = la holds a token
} parser_t;

static void p_init(parser_t *P, const char *s) {
    P->L.s = s; P->L.i = 0; P->have_la = 0;
    P->la.kind = TK_ERR; P->la.lexeme = NULL;
}
static token_t p_peek(parser_t *P) {
    if (!P->have_la) { P->la = lex_next(&P->L); P->have_la = 1; }
    return P->la;
}
static token_t p_get(parser_t *P) {
    token_t t = p_peek(P);
    P->have_la = 0;
    return t;
}

// ---------- AST helpers ----------
static void redir_init(redir_t *r) {
    r->in_path = NULL; r->out_path = NULL; r->append_path = NULL;
}
static void cmd_init(cmd_t *c) {
    c->argv = NULL;
    redir_init(&c->redir);
}

static int push_arg(cmd_t *c, const char *w) {
    size_t n = 0;
    if (c->argv) { while (c->argv[n]) n++; }
    char **nv = xmalloc(sizeof(char*) * (n + 2));
    for (size_t i = 0; i < n; ++i) nv[i] = c->argv[i];
    nv[n] = xstrdup(w);
    nv[n+1] = NULL;
    free(c->argv);
    c->argv = nv;
    return 0;
}
static int set_once(char **slot, const char *path) {
    if (*slot) return -1;
    *slot = xstrdup(path);
    return 0;
}

// Parse a single pipeline stage: WORDs and redirections, stopping before |, &, or EOL.
// Returns 0 on success, nonzero on syntax error. Sets *saw_word if any WORD occurred.
static int parse_stage(parser_t *P, cmd_t *out, int *saw_word) {
    cmd_init(out);
    *saw_word = 0;

    for (;;) {
        token_t t = p_peek(P);
        switch (t.kind) {
            case TK_WORD:
                (void)p_get(P);
                *saw_word = 1;
                push_arg(out, t.lexeme);
                free(t.lexeme);
                break;

            case TK_LT: {
                (void)p_get(P);
                token_t a = p_get(P);
                if (a.kind != TK_WORD) return -1; // need a path
                if (set_once(&out->redir.in_path, a.lexeme) < 0) { free(a.lexeme); return -1; }
                free(a.lexeme);
                break;
            }
            case TK_GT: {
                (void)p_get(P);
                token_t a = p_get(P);
                if (a.kind != TK_WORD) return -1;
                if (set_once(&out->redir.out_path, a.lexeme) < 0) { free(a.lexeme); return -1; }
                free(a.lexeme);
                break;
            }
            case TK_DGT: {
                (void)p_get(P);
                token_t a = p_get(P);
                if (a.kind != TK_WORD) return -1;
                if (set_once(&out->redir.append_path, a.lexeme) < 0) { free(a.lexeme); return -1; }
                free(a.lexeme);
                break;
            }

            // Stage terminators: do NOT consume; let caller handle
            case TK_BAR:
            case TK_AMP:
            case TK_EOL:
                return 0;

            case TK_ERR:
            default:
                return -1;
        }
    }
}

int parse_line(const char *line, pipeline_t *out) {
    if (!line || !out) return -1;
    memset(out, 0, sizeof(*out));

    parser_t P; p_init(&P, line);

    cmd_t *stages = NULL; int n = 0;

    for (;;) {
        int saw = 0; cmd_t c;
        if (parse_stage(&P, &c, &saw) != 0) goto syntax_err;

        if (!saw) goto syntax_err; // empty stage like "|" or blank

        // redir conflict: cannot have both > and >>
        if (c.redir.out_path && c.redir.append_path) {
            // cleanup this stage before failing
            if (c.argv) { for (char **p = c.argv; *p; ++p) free(*p); free(c.argv); }
            free(c.redir.in_path); free(c.redir.out_path); free(c.redir.append_path);
            goto syntax_err;
        }

        cmd_t *nv = realloc(stages, sizeof(cmd_t) * (n + 1));
        if (!nv) { perror("realloc"); exit(1); }
        stages = nv; stages[n++] = c;

        token_t sep = p_peek(&P);
        if (sep.kind == TK_BAR) {
            (void)p_get(&P);    // consume '|', continue to next stage
            continue;
        }
        if (sep.kind == TK_AMP) {
            (void)p_get(&P);    // consume '&'
            out->background = 1;
            sep = p_peek(&P);
            if (sep.kind != TK_EOL) {
                // Any non-EOL after '&' is a syntax error (only trailing & allowed)
                goto syntax_err;
            }
        }
        if (sep.kind == TK_EOL) {
            // All done
            break;
        }

        // Any other token here is unexpected
        goto syntax_err;
    }

    if (n == 0) goto syntax_err;

    out->stages = stages;
    out->nstages = n;
    return 0;

syntax_err:
    if (stages) {
        for (int i = 0; i < n; ++i) {
            if (stages[i].argv) { for (char **p = stages[i].argv; *p; ++p) free(*p); free(stages[i].argv); }
            free(stages[i].redir.in_path);
            free(stages[i].redir.out_path);
            free(stages[i].redir.append_path);
        }
        free(stages);
    }
    memset(out, 0, sizeof(*out));
    return -1;
}

void free_pipeline(pipeline_t *pl) {
    if (!pl || !pl->stages) return;
    for (int i = 0; i < pl->nstages; ++i) {
        cmd_t *c = &pl->stages[i];
        if (c->argv) { for (char **p = c->argv; *p; ++p) free(*p); free(c->argv); }
        free(c->redir.in_path);
        free(c->redir.out_path);
        free(c->redir.append_path);
    }
    free(pl->stages);
    pl->stages = NULL;
    pl->nstages = 0;
    pl->background = 0;
}

char *argv_join(char *const argv[]) {
    size_t len = 0;
    for (int i = 0; argv && argv[i]; ++i) len += strlen(argv[i]) + 1;
    char *s = xmalloc(len + 1);
    s[0] = '\0';
    for (int i = 0; argv && argv[i]; ++i) {
        strcat(s, argv[i]);
        if (argv[i+1]) strcat(s, " ");
    }
    return s;
}
