#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Token kinds produced by the lexer ----
typedef enum {
    TK_WORD,  // e.g., "echo", "file.txt" (after quote/escape processing)
    TK_LT,    // <
    TK_GT,    // >
    TK_DGT,   // >>
    TK_BAR,   // |
    TK_AMP,   // & (must be trailing; applies to whole pipeline)
    TK_EOL,   // end of line/input
    TK_ERR    // lexer error (unclosed quote, bad escape, etc.)
} token_kind_t;

// A single token. 'lexeme' is only set for TK_WORD (malloc'd).
typedef struct {
    token_kind_t kind;
    char *lexeme;          // NULL unless kind == TK_WORD
} token_t;

// Per-command (stage) redirections
typedef struct {
    char *in_path;         // for '<'  (nullable)
    char *out_path;        // for '>'  (truncate; nullable)
    char *append_path;     // for '>>' (append; nullable)
} redir_t;

// One pipeline stage: argv + redirections
typedef struct {
    char **argv;           // NULL-terminated; argv[0] is the program
    redir_t redir;         // redirection info
} cmd_t;

// A full parsed line: one or more stages possibly piped together
typedef struct {
    cmd_t *stages;         // array of stages
    int    nstages;        // number of stages
    int    background;     // 1 if trailing '&'
} pipeline_t;

// Parse a command line into a pipeline AST. Returns 0 on success, nonzero on syntax error.
int parse_line(const char *line, pipeline_t *out);

// Free all allocations inside 'out' (safe to call on a zeroed struct).
void free_pipeline(pipeline_t *pl);

// Join argv into a printable string (caller frees).
char *argv_join(char *const argv[]);

#ifdef __cplusplus
}
#endif
