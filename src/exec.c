#include "exec.h"
#include <unistd.h>
#include <sys/wait.h>
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

int run_command(const char *abs_path, char *const argv[],
                const exec_opts_t *opts, pid_t *out_pid, int *out_status){
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
