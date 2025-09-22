#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
TK_WORD,
TK_LT, // <
TK_GT, // >
TK_DGT, // >>
TK_BAR, // |
TK_AMP, // & (must be trailing)
TK_EOL,
TK_ERR
} token_kind_t;

typedef struct {
token_kind_t kind;
char *lexeme; // only for TK_WORD (malloc'd)
} token_t;

typedef struct {
char *in_path; // nullable
char *out_path; // nullable (truncate)
char *append_path; // nullable (append)
} redir_t;

typedef struct {
char **argv; // NULL-terminated (malloc'd strings)
redir_t redir;
} cmd_t;

typedef struct {
cmd_t *stages; // array of stages
int nstages;
int background; // 1 if trailing &
} pipeline_t;

int parse_line(const char *line, pipeline_t *out);
void free_pipeline(pipeline_t *pl);
char *argv_join(char *const argv[]);

#ifdef __cplusplus
}
#endif
