#pragma once
#include <sys/types.h>
#include <stdbool.h>

/*
 * Person B:
 * - Pass these fds when you set up redirection or a pipeline.
 * - Use -1 to inherit the shell's current stdio for any stream you don't override.
 * - For pipelines, you’ll dup2() in your own code OR you can just pass the ends
 *   you want the child to inherit via this struct — Person A's code will dup2().
 */

typedef struct {
    int in_fd;  
    int out_fd;
    int err_fd;
    bool background;
} exec_opts_t;

/*
 * run_command:
 *  - abs_path must be an absolute path OR contain a '/' (restriction: no execvp).
 *  - argv must be NULL-terminated (argv[0] = program name, argv[n] = NULL).
 *  - If opts->background == true: parent does NOT wait.
 *    * out_pid receives the child's PID so Person A can register a background job.
 *  - If background == false: function waits and returns only when child exits.
 *    * out_status receives the waitpid() status (use WIFEXITED/WEXITSTATUS).
 *
 * returns 0 on success (spawned child), -1 on fork/wait errors.
 */

int run_command(const char *abs_path, char *const argv[],
                const exec_opts_t *opts, pid_t *out_pid, int *out_status);

/* Convenience wait wrapper for a single foreground child. */
int wait_for_child(pid_t pid, int *out_status);