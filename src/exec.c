// =============================
// File: src/exec.c
// =============================
#define _POSIX_C_SOURCE 200809L
#include "exec.h"
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

/* Apply caller-provided stdio fds (Person B passes them via exec_opts_t). 
   RETURN -1 on error so the child can fail fast. */
static int apply_fds(const exec_opts_t *o){
    if (!o) return 0;
    if (o->in_fd >= 0 && dup2(o->in_fd, 0) < 0) return -1;
    if (o->out_fd >= 0 && dup2(o->out_fd, 1) < 0) return -1;
    if (o->err_fd >= 0 && dup2(o->err_fd, 2) < 0) return -1;
    return 0;
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

int run_command(const char *abs_path, char *const argv[],
                const exec_opts_t *opts, pid_t *out_pid, int *out_status){
    pid_t pid = fork();
    if (pid < 0){ 
        perror("fork"); 
        return -1; 
    }

    if (pid == 0){
        // ---- child ----
        fprintf(stderr, "[child] starting %s in_fd=%d out_fd=%d err_fd=%d\n", 
                abs_path, 
                opts ? opts->in_fd : -1, 
                opts ? opts->out_fd : -1, 
                opts ? opts->err_fd : -1);
        
        fprintf(stderr, "[child] argv: ");
        for(int i = 0; argv && argv[i]; i++) {
            fprintf(stderr, "[%d]='%s' ", i, argv[i]);
        }
        fprintf(stderr, "\n");
        
        if (opts) {
            if (apply_fds(opts) < 0) {
                fprintf(stderr, "dup2 failed: %s\n", strerror(errno));
                _exit(126);
            }
            fprintf(stderr, "[child] dup2 successful, closing original fds\n");
            // Close originals after dup2 to avoid leaking descriptors
            if (opts->in_fd >= 0) close(opts->in_fd);
            if (opts->out_fd >= 0) close(opts->out_fd);
            if (opts->err_fd >= 0) close(opts->err_fd);
        }

        fprintf(stderr, "[child] about to execv %s\n", abs_path);
        execv(abs_path, argv); // project rule: no PATH search
        fprintf(stderr, "exec failed: %s: %s\n", abs_path, strerror(errno));
        _exit(127);
    }

    // ---- parent ----
    if (out_pid) *out_pid = pid;
    if (opts && opts->background){
        // caller will track background job; don't wait
        fprintf(stderr, "[parent] background process %d started, not waiting\n", (int)pid);
        return 0;
    }
    fprintf(stderr, "[parent] waiting for child %d\n", (int)pid);
    int result = wait_for_child(pid, out_status);
    fprintf(stderr, "[parent] child %d finished with result %d\n", (int)pid, result);
    return result;
}