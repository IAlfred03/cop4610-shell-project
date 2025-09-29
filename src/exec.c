// src/exec.c
#define _POSIX_C_SOURCE 200809L
#include "exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

/* Apply caller-provided stdio fds (Person B passes them via exec_opts_t). */
static void apply_fds(const exec_opts_t *o){
    if (!o) return;
    if (o->in_fd  >= 0) dup2(o->in_fd,  STDIN_FILENO);
    if (o->out_fd >= 0) dup2(o->out_fd, STDOUT_FILENO);
    if (o->err_fd >= 0) dup2(o->err_fd, STDERR_FILENO);
}

int wait_for_child(pid_t pid, int *out_status){
    int status;
    if (waitpid(pid, &status, 0) < 0){
        perror("waitpid");
        return -1;
    }
    if (out_status) *out_status = status;
    return 0;
}

/* ----- Argument expansion (basic) ----- */
char *expand_arg(const char *arg) {
    if (!arg) return NULL;

    /* Tilde expansion: ~ -> $HOME */
    if (arg[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = "";
        size_t len = strlen(home) + strlen(arg);
        char *res = (char*)malloc(len + 1);
        if (!res) { perror("malloc"); return NULL; }
        strcpy(res, home);
        strcat(res, arg + 1);
        return res;
    }

    /* Environment variable: $VAR */
    if (arg[0] == '$') {
        const char *p = arg + 1;
        char var[256]; int i = 0;
        while (*p && (isalnum((unsigned char)*p) || *p == '_') && i < 255) {
            var[i++] = *p++;
        }
        var[i] = '\0';
        const char *val = getenv(var);
        return strdup(val ? val : "");
    }

    return strdup(arg);
}

/* ----- run_command implementation ----- */
int run_command(const char *abs_path,
                char *const argv[],
                const exec_opts_t *opts,
                pid_t *out_pid,
                int *out_status)
{
    if (!abs_path || !argv) { errno = EINVAL; return -1; }

    pid_t pid = fork();
    if (pid < 0){
        perror("fork");
        return -1;
    }

    if (pid == 0){
        if (opts) apply_fds(opts);
        execv(abs_path, argv); /* no execvp per project rules */
        fprintf(stderr, "exec failed: %s: %s\n", abs_path, strerror(errno));
        _exit(127);
    }

    if (out_pid) *out_pid = pid;

    if (opts && opts->background){
        /* Caller (harness) will register background job; don't wait */
        return 0;
    }

    return wait_for_child(pid, out_status);
}
