// include/exec.h  — unified header (Person A launcher + Person B executor)
#pragma once
#include <sys/types.h>
#include <stdbool.h>
#include "parser.h"   // for pipeline_t

#ifdef __cplusplus
extern "C" {
#endif

/* Options for launching a single command (stdio wiring + bg/fg). 
   Use -1 to inherit the shell's current fd for any stream. 
   Note: err_fd is reserved; we don't parse 2> yet. */
typedef struct {
    int  in_fd;       // -1 = inherit STDIN
    int  out_fd;      // -1 = inherit STDOUT
    int  err_fd;      // -1 = inherit STDERR
    bool background;  // true = do not wait in launcher
} exec_opts_t;

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

/* Optional convenience (A-side). */
int wait_for_child(pid_t pid, int *out_status);

/* Person B entry point: execute a parsed pipeline using run_command().
   - Handles pipes and redirections (<, >, >>) per stage.
   - Single-stage builtins (cd/pwd/exit) run in the parent with redirs applied.
   - Sets one process group (pgid = first child pid) for C's job control.
   - If pl->background == 1: do not wait; else wait for all children.
   Returns 0 on success, non-zero on failure. */
int exec_pipeline(const pipeline_t *pl);

#ifdef __cplusplus
}
#endif
