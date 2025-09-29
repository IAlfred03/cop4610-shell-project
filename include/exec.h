// include/exec.h — unified header (Person A launcher + Person B executor)
#pragma once
#include <sys/types.h>
#include <stdbool.h>
#include "parser.h"

#ifdef __cplusplus
extern "C" {
#endif


/* Options for launching a single command (stdio wiring + bg/fg).
   Use -1 to inherit the shell's current fd for any stream.
   Note: err_fd is reserved; we don't parse 2> yet. */
typedef struct exec_opts {
    int  in_fd;       // -1 = inherit STDIN
    int  out_fd;      // -1 = inherit STDOUT
    int  err_fd;      // -1 = inherit STDERR
    bool background;  // true = do not wait in launcher
} exec_opts_t;

/* Execute a full pipeline (already parsed).
   Returns 0 on success, non-zero on failure. */
int exec_pipeline(const pipeline_t *pl);

/* Person A launcher (implemented elsewhere; do not implement in Person B).
   - abs_path must be absolute OR contain '/' (no PATH search via execvp).
   - argv is NULL-terminated (argv[0] = program).
   - If opts->background == true: parent does NOT wait; out_pid gets child PID.
   - If opts->background == false: waits; out_status gets waitpid() status.
   Returns 0 on success, -1 on fork/exec/wait errors. */
int run_command(const char *abs_path,
                char *const argv[],
                const exec_opts_t *opts,
                pid_t *out_pid,
                int *out_status);

/* Convenience wait wrapper for a single foreground child. */
int wait_for_child(pid_t pid, int *out_status);

#ifdef __cplusplus
}
#endif
