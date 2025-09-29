#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "exec.h"
#include "builtins.h"
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

/* Helper function to open redirection files */
static int open_redir_files(const redir_t *redir, int *in_fd, int *out_fd) {
    *in_fd = -1;
    *out_fd = -1;

    if (redir->in_path) {
        *in_fd = open(redir->in_path, O_RDONLY);
        if (*in_fd < 0) {
            perror(redir->in_path);
            return -1;
        }
    }

    if (redir->out_path) {
        mkdir("tests/tmp", 0755);
        *out_fd = open(redir->out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (*out_fd < 0) {
            perror(redir->out_path);
            if (*in_fd >= 0) close(*in_fd);
            return -1;
        }
    } else if (redir->append_path) {
        mkdir("tests/tmp", 0755);
        *out_fd = open(redir->append_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (*out_fd < 0) {
            perror(redir->append_path);
            if (*in_fd >= 0) close(*in_fd);
            return -1;
        }
    }

    return 0;
}

/* Helper: run a builtin in the parent with possible redirections */
static int run_builtin_with_redir(cmd_t *cmd) {
    int saved_stdin = -1, saved_stdout = -1;
    int in_fd = -1, out_fd = -1;
    int result = 0;

    if (open_redir_files(&cmd->redir, &in_fd, &out_fd) != 0) return -1;

    if (in_fd >= 0) {
        saved_stdin = dup(0);
        if (saved_stdin < 0 || dup2(in_fd, 0) < 0) {
            perror("dup2 stdin");
            result = -1;
            goto cleanup;
        }
    }

    if (out_fd >= 0) {
        saved_stdout = dup(1);
        if (saved_stdout < 0 || dup2(out_fd, 1) < 0) {
            perror("dup2 stdout");
            result = -1;
            goto cleanup;
        }
    }

    result = run_builtin_parent(cmd->argv);

cleanup:
    if (saved_stdin >= 0) { dup2(saved_stdin, 0); close(saved_stdin); }
    if (saved_stdout >= 0) { dup2(saved_stdout, 1); close(saved_stdout); }
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    return result;
}

int exec_pipeline(const pipeline_t *pl) {
    if (!pl || pl->nstages == 0) {
        fprintf(stderr, "exec_pipeline: empty pipeline\n");
        return -1;
    }

    /* Single-stage fast path */
    if (pl->nstages == 1) {
        cmd_t *cmd = &pl->stages[0];

        if (cmd->argv && cmd->argv[0] && is_builtin(cmd->argv[0])) {
            /* Builtin runs in parent so it can affect shell state (e.g., cd) */
            return run_builtin_with_redir(cmd);
        }

        /* External command with optional redirs */
        int in_fd, out_fd;
        if (open_redir_files(&cmd->redir, &in_fd, &out_fd) != 0) return -1;

        exec_opts_t opts = { in_fd, out_fd, -1, pl->background };
        pid_t pid = -1;
        int status = 0;
        int rc = run_command(cmd->argv[0], cmd->argv, &opts, &pid, &status);

        if (in_fd >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);

        if (rc != 0) return -1;
        if (pl->background) return 0; /* caller handles jobs list */
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    /* Multi-stage pipeline */
    pid_t *pids = malloc(pl->nstages * sizeof(pid_t));
    if (!pids) return -1;

    int **pipes = NULL;
    if (pl->nstages > 1) {
        pipes = malloc((pl->nstages - 1) * sizeof(int*));
        if (!pipes) { free(pids); return -1; }
        for (int i = 0; i < pl->nstages - 1; i++) {
            pipes[i] = malloc(2 * sizeof(int));
            if (!pipes[i] || pipe(pipes[i]) < 0) {
                perror("pipe");
                for (int j = 0; j <= i; j++) {
                    if (pipes[j]) {
                        if (pipes[j][0]) close(pipes[j][0]);
                        if (pipes[j][1]) close(pipes[j][1]);
                        free(pipes[j]);
                    }
                }
                free(pipes);
                free(pids);
                return -1;
            }
        }
    }

    for (int i = 0; i < pl->nstages; i++) {
        cmd_t *cmd = &pl->stages[i];
        int in_fd = -1, out_fd = -1;

        /* Input setup */
        if (i == 0) {
            if (cmd->redir.in_path) {
                in_fd = open(cmd->redir.in_path, O_RDONLY);
                if (in_fd < 0) { perror(cmd->redir.in_path); goto pipeline_cleanup; }
            } else {
                in_fd = -1;
            }
        } else {
            in_fd = pipes[i-1][0];
        }

        /* Output setup */
        if (i == pl->nstages - 1) {
            if (cmd->redir.out_path) {
                mkdir("tests/tmp", 0755);
                out_fd = open(cmd->redir.out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd < 0) { perror(cmd->redir.out_path); if (i==0 && in_fd>=0 && cmd->redir.in_path) close(in_fd); goto pipeline_cleanup; }
            } else if (cmd->redir.append_path) {
                mkdir("tests/tmp", 0755);
                out_fd = open(cmd->redir.append_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (out_fd < 0) { perror(cmd->redir.append_path); if (i==0 && in_fd>=0 && cmd->redir.in_path) close(in_fd); goto pipeline_cleanup; }
            } else {
                out_fd = -1;
            }
        } else {
            out_fd = pipes[i][1];
        }

        if (cmd->argv && cmd->argv[0] && is_builtin(cmd->argv[0])) {
            /* Builtins in pipelines must run in a child */
            pids[i] = fork();
            if (pids[i] < 0) { perror("fork"); goto pipeline_cleanup; }
            if (pids[i] == 0) {
                if (in_fd >= 0) dup2(in_fd, 0);
                if (out_fd >= 0) dup2(out_fd, 1);

                for (int j = 0; j < pl->nstages - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                if (i == 0 && in_fd >= 0 && cmd->redir.in_path) close(in_fd);
                if (i == pl->nstages - 1 && out_fd >= 0 &&
                    (cmd->redir.out_path || cmd->redir.append_path)) close(out_fd);

                int rc = run_builtin_parent(cmd->argv);
                _exit(rc);
            }
        } else {
            /* External command: non-waiting launch; we'll wait after all are spawned */
            exec_opts_t opts = { in_fd, out_fd, -1, true };
            if (run_command(cmd->argv[0], cmd->argv, &opts, &pids[i], NULL) != 0) {
                goto pipeline_cleanup;
            }
        }

        /* Parent: close ends that this stage used */
        if (i < pl->nstages - 1) close(pipes[i][1]);     /* writer closed after launch */
        if (i > 0)                close(pipes[i-1][0]);   /* reader closed after launch */

        if (i == 0 && in_fd >= 0 && cmd->redir.in_path) close(in_fd);
        if (i == pl->nstages - 1 && out_fd >= 0 &&
            (cmd->redir.out_path || cmd->redir.append_path)) close(out_fd);
    }

    /* Wait (foreground only) */
    int final_status = 0;
    if (!pl->background) {
        fprintf(stderr, "[DEBUG] Waiting for %d pipeline processes\n", pl->nstages);
        for (int i = 0; i < pl->nstages; i++) {
            int status;
            fprintf(stderr, "[DEBUG] Waiting for process %d (pid %d)\n", i, (int)pids[i]);
            pid_t r = waitpid(pids[i], &status, 0);
            if (r < 0) {
                perror("waitpid");
                final_status = -1;
            } else {
                fprintf(stderr, "[DEBUG] Process %d finished with status %d\n", i, status);
                if (i == pl->nstages - 1) {
                    final_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                }
            }
        }
        fprintf(stderr, "[DEBUG] All pipeline processes finished\n");
    }

    for (int i = 0; i < pl->nstages - 1; i++) free(pipes[i]);
    free(pipes);
    free(pids);
    return final_status;

pipeline_cleanup:
    if (pipes) {
        for (int i = 0; i < pl->nstages - 1; i++) {
            if (pipes[i]) {
                close(pipes[i][0]);
                close(pipes[i][1]);
                free(pipes[i]);
            }
        }
        free(pipes);
    }
    free(pids);
    return -1;
}
