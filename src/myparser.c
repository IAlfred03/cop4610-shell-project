#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../include/myparser.h"

// Token kinds
typedef enum {
    TK_WORD,
    TK_LT,   // <
    TK_GT,   // >
    TK_DGT,  // >>
    TK_BAR,  // |
    TK_AMP,  // & (must be trailing)
    TK_EOL,
    TK_ERR
} token_kind_t;

typedef struct {
    token_kind_t kind;
    char *lexeme; // only for TK_WORD (malloc'd)
} token_t;

typedef struct {
    char *in_path;      // nullable
    char *out_path;     // nullable (truncate)
    char *append_path;  // nullable (append)
} redir_t;

typedef struct {
    char **argv;   // NULL-terminated (malloc'd strings)
    redir_t redir;
} cmd_t;

typedef struct {
    cmd_t *stages;  // array of stages
    int nstages;
    int background; // 1 if trailing &
} pipeline_t;

// ============================================================
// Expansion Helpers
// ============================================================
static char *expand_token(const char *tok) {
    // Environment variable expansion
    if (tok[0] == '$') {
        const char *val = getenv(tok + 1);
        if (val) return strdup(val);
        return strdup(""); // undefined vars -> empty string
    }

    // Tilde expansion
    if (tok[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = "";
        if (tok[1] == '\0') {
            return strdup(home);
        } else if (tok[1] == '/') {
            size_t len = strlen(home) + strlen(tok);
            char *out = malloc(len + 1);
            snprintf(out, len + 1, "%s%s", home, tok + 1);
            return out;
        }
    }

    // Default: no expansion
    return strdup(tok);
}

// ============================================================
// Parser Implementation (stub example)
// ============================================================
int parse_line(const char *line, char ***argv_out, int *background_out) {
    // For now: very minimal split on spaces
    // (Replace this with real tokenizer if needed)
    char *copy = strdup(line);
    if (!copy) return -1;

    int cap = 8;
    int argc = 0;
    char **argv = malloc(sizeof(char*) * cap);

    char *tok = strtok(copy, " \t\n");
    while (tok) {
        if (argc + 1 >= cap) {
            cap *= 2;
            argv = realloc(argv, sizeof(char*) * cap);
        }
        argv[argc++] = expand_token(tok);
        tok = strtok(NULL, " \t\n");
    }

    argv[argc] = NULL;
    *argv_out = argv;
    *background_out = 0; // not implemented yet

    free(copy);
    return 0;
}

void free_argv(char **argv) {
    if (!argv) return;
    for (int i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}

// ============================================================
// Extra utility for debugging
// ============================================================
char *argv_join(char *const argv[]) {
    if (!argv) return NULL;
    size_t len = 0;
    for (int i = 0; argv[i]; i++) len += strlen(argv[i]) + 1;

    char *buf = malloc(len + 1);
    buf[0] = '\0';
    for (int i = 0; argv[i]; i++) {
        strcat(buf, argv[i]);
        if (argv[i + 1]) strcat(buf, " ");
    }
    return buf;
}
