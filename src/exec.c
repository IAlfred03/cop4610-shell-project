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
#include <pwd.h>        /* getpwuid */
#include <sys/types.h>

/* Apply caller-provided stdio fds (Person B passes them via exec_opts_t). */
static void apply_fds(const exec_opts_t *o){
    if (!o) return;
    if (o->in_fd  >= 0)  { fprintf(stderr, "[exec] dup2(in_fd=%d -> 0)\n",  o->in_fd);  dup2(o->in_fd,  STDIN_FILENO); }
    if (o->out_fd >= 0)  { fprintf(stderr, "[exec] dup2(out_fd=%d -> 1)\n", o->out_fd); dup2(o->out_fd, STDOUT_FILENO); }
    if (o->err_fd >= 0)  { fprintf(stderr, "[exec] dup2(err_fd=%d -> 2)\n", o->err_fd); dup2(o->err_fd, STDERR_FILENO); }
}

int wait_for_child(pid_t pid, int *out_status){
    int status;
    fprintf(stderr, "[exec] waiting for pid=%d\n", (int)pid);
    if (waitpid(pid, &status, 0) < 0){
        perror("waitpid");
        return -1;
    }
    fprintf(stderr, "[exec] pid=%d finished: raw_status=%d (WIFEXITED=%d, code=%d)\n",
            (int)pid, status, WIFEXITED(status), WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    if (out_status) *out_status = status;
    return 0;
}

/* ----- Argument expansion (with robust ~ and $VAR) ----- */
char *expand_arg(const char *arg) {
    if (!arg) return NULL;

    /* Tilde expansion: ~ -> $HOME (fallback to pw_dir) */
    if (arg[0] == '~') {
        const char *home = getenv("HOME");
        if (!home || !*home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw && pw->pw_dir) home = pw->pw_dir;
        }
        if (!home) home = "";
        size_t len = strlen(home) + strlen(arg);
        char *res = (char*)malloc(len + 1);
        if (!res) { perror("malloc"); return NULL; }
        strcpy(res, home);
        strcat(res, arg + 1);
        fprintf(stderr, "[exec] expand_arg: '%s' -> '%s' (tilde)\n", arg, res);
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
        char *out = strdup(val ? val : "");
        if (!out) { perror("strdup"); return NULL; }
        fprintf(stderr, "[exec] expand_arg: '%s' -> '%s' ($VAR)\n", arg, out);
        return out;
    }

    /* No expansion */
    char *copy = strdup(arg);
    if (!copy) { perror("strdup"); return NULL; }
    fprintf(stderr, "[exec] expand_arg: '%s' (no change)\n", arg);
    return copy;
}

/* ----- run_command implementation (execv only; no PATH search) ----- */
int run_command(const char *abs_path,
                char *const argv[],
                const exec_opts_t *opts,
                pid_t *out_pid,
                int *out_status)
{
    if (!abs_path || !argv) { errno = EINVAL; return -1; }

    fprintf(stderr, "[exec] run_command: path='%s' bg=%d in=%d out=%d err=%d\n",
            abs_path,
            (int)(opts ? opts->background : 0),
            (opts ? opts->in_fd  : -2),
            (opts ? opts->out_fd : -2),
            (opts ? opts->err_fd : -2));
    if (argv){
        fprintf(stderr, "[exec] argv:");
        for (int i=0; argv[i]; ++i) fprintf(stderr, " [%d]='%s'", i, argv[i]);
        fprintf(stderr, "\n");
    }

    pid_t pid = fork();
    if (pid < 0){
        perror("fork");
        return -1;
    }

    if (pid == 0){
        /* Child */
        if (opts) apply_fds(opts);
        execv(abs_path, argv); /* no execvp per project rules */
        fprintf(stderr, "exec failed: %s: %s\n", abs_path, strerror(errno));
        _exit(127);
    }

    /* Parent */
    if (out_pid) *out_pid = pid;

    if (opts && opts->background){
        /* Caller (harness) will register background job; don't wait */
        fprintf(stderr, "[exec] background launch pid=%d (no wait)\n", (int)pid);
        return 0;
    }

    return wait_for_child(pid, out_status);
}
