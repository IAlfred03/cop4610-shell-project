#include "exec.h"
#include "parser.h"
#include "jobs.h"   // use jobs.c API

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

/* Apply caller-provided stdio fds (Person B passes them via exec_opts_t). */
static void apply_fds(const exec_opts_t *o){
    if (!o) return;
    if (o->in_fd  >= 0) dup2(o->in_fd,  0);
    if (o->out_fd >= 0) dup2(o->out_fd, 1);
    if (o->err_fd >= 0) dup2(o->err_fd, 2);
}

int wait_for_child(pid_t pid, int *out_status){
    int status;
    if (waitpid(pid, &status, 0) < 0){ perror("waitpid"); return -1; }
    if (out_status) *out_status = status;
    return 0;
}
typedef struct {
    int saved_stdin;
    int saved_stdout;
    int saved_stderr;
} saved_fds_t;
static void restore_redirs_in_parent(const saved_fds_t *saved) {
    if (saved->saved_stdin!=-1){dup2(saved->saved_stdin,STDIN_FILENO); close(saved->saved_stdin);}
    if (saved->saved_stdout!=-1){dup2(saved->saved_stdout,STDOUT_FILENO); close(saved->saved_stdout);}
    if (saved->saved_stderr!=-1){dup2(saved->saved_stderr,STDERR_FILENO); close(saved->saved_stderr);}
}

/* ----- Argument expansion ----- */
char *expand_arg(const char *arg) {
    if (!arg) return NULL;

    // Tilde expansion
    if (arg[0]=='~') {
        const char *home = getenv("HOME");
        if (!home) home="";
        size_t len = strlen(home) + strlen(arg);
        char *res = malloc(len + 1);
        strcpy(res, home);
        strcat(res, arg+1);
        return res;
    }

    // Environment variable expansion
    if (arg[0]=='$') {
        const char *p = arg + 1;
        char var[256]; int i=0;
        while ((*p && (isalnum((unsigned char)*p) || *p=='_')) && i<255) var[i++] = *p++;
        var[i]='\0';
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
                int *out_status) {
    if (!abs_path || !argv) { errno=EINVAL; return -1; }

    pid_t pid = fork();
    if (pid < 0){ perror("fork"); return -1; }
    if (pid == 0){
        if (opts) apply_fds(opts);
        execv(abs_path, argv); // no execvp
        fprintf(stderr, "exec failed: %s: %s\n", abs_path, strerror(errno));
        _exit(127);
    }
    if (out_pid) *out_pid = pid;
    if (opts && opts->background){
        // caller will jobs_register()
        return 0;
    }
    return wait_for_child(pid, out_status);
}