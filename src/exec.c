// src/exec.c
#define _POSIX_C_SOURCE 200809L
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
#include <signal.h>
#include <ctype.h>   // for isalnum

/* ----- Helpers ----- */
typedef struct { int saved_stdin, saved_stdout, saved_stderr; } saved_fds_t;

static char *argv_join_local(char *const argv[]) {
    if (!argv || !argv[0]) return strdup("");
    size_t len = 0;
    for (char *const *p = argv; *p; ++p) len += strlen(*p) + 1;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    s[0] = '\0';
    for (char *const *p = argv; *p; ++p) {
        if (p != argv) strcat(s, " ");
        strcat(s, *p);
    }
    return s;
}

static char *lookup_in_path(const char *cmd) {
    if (!cmd) return NULL;
    if (strchr(cmd, '/')) return strdup(cmd);
    const char *path = getenv("PATH");
    if (!path) return NULL;
    char *pathdup = strdup(path);
    if (!pathdup) return NULL;
    char *saveptr = NULL;
    char *token = strtok_r(pathdup, ":", &saveptr);
    char candidate[4096];
    while (token) {
        snprintf(candidate, sizeof(candidate), "%s/%s", token, cmd);
        if (access(candidate, X_OK) == 0) {
            free(pathdup);
            return strdup(candidate);
        }
        token = strtok_r(NULL, ":", &saveptr);
    }
    free(pathdup);
    return NULL;
}

/* ----- Redirection helpers ----- */
static int apply_redir_paths(const redir_t *r, exec_opts_t *opts) {
    opts->in_fd = -1; opts->out_fd = -1; opts->err_fd = -1;
    if (!r) return 0;
    if (r->in_path) {
        int fd = open(r->in_path, O_RDONLY);
        if (fd < 0) return -1;
        opts->in_fd = fd;
    }
    if (r->out_path) {
        int fd = open(r->out_path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
        if (fd < 0) return -1;
        opts->out_fd = fd;
    }
    if (r->append_path) {
        int fd = open(r->append_path, O_WRONLY|O_CREAT|O_APPEND, 0666);
        if (fd < 0) return -1;
        opts->out_fd = fd;
    }
    return 0;
}

static int apply_redirs_in_parent(const redir_t *r, saved_fds_t *saved) {
    saved->saved_stdin = saved->saved_stdout = saved->saved_stderr = -1;
    if (!r) return 0;
    if (r->in_path) {
        int fd = open(r->in_path, O_RDONLY);
        if (fd < 0) return -1;
        saved->saved_stdin = dup(STDIN_FILENO);
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (r->out_path) {
        int fd = open(r->out_path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
        if (fd < 0) return -1;
        saved->saved_stdout = dup(STDOUT_FILENO);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    if (r->append_path) {
        int fd = open(r->append_path, O_WRONLY|O_CREAT|O_APPEND, 0666);
        if (fd < 0) return -1;
        if (saved->saved_stdout == -1) saved->saved_stdout = dup(STDOUT_FILENO);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    return 0;
}

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
    if (pid < 0) return -1;

    if (pid == 0) {
        if (opts) {
            if (opts->in_fd!=-1) dup2(opts->in_fd, STDIN_FILENO);
            if (opts->out_fd!=-1) dup2(opts->out_fd, STDOUT_FILENO);
            if (opts->err_fd!=-1) dup2(opts->err_fd, STDERR_FILENO);
        }
        execv(abs_path, argv);
        fprintf(stderr,"exec failed: %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    if (out_pid) *out_pid = pid;

    if (opts && opts->background) {
        char *line = argv_join_local(argv);
        jobs_register(jobs_next_id(), pid, line);  // register background job
        free(line);
        return 0;
    }

    int status=0;
    if (waitpid(pid,&status,0)<0) return -1;
    if (out_status) *out_status = status;
    return 0;
}

/* ----- exec_pipeline implementation ----- */
int exec_pipeline(const pipeline_t *pl) {
    if (!pl || pl->nstages==0) return -1;

    if (pl->nstages==1) {
        cmd_t *cmd = &pl->stages[0];
        if (!cmd->argv || !cmd->argv[0]) return -1;
        char *const *argv = cmd->argv;

        // builtins
        if (strcmp(argv[0],"cd")==0) {
            saved_fds_t saved={-1,-1,-1};
            apply_redirs_in_parent(&cmd->redir,&saved);
            const char *target = argv[1]?argv[1]:getenv("HOME");
            int rc = (!target || chdir(target)!=0)?-1:0;
            restore_redirs_in_parent(&saved);
            return rc;
        }
        if (strcmp(argv[0],"exit")==0) return 0;
        if (strcmp(argv[0],"pwd")==0) {
            saved_fds_t saved={-1,-1,-1};
            apply_redirs_in_parent(&cmd->redir,&saved);
            char cwd[4096];
            if (getcwd(cwd,sizeof(cwd))) { printf("%s\n",cwd); fflush(stdout); }
            restore_redirs_in_parent(&saved);
            return 0;
        }
        if (strcmp(argv[0],"jobs")==0) {
            saved_fds_t saved={-1,-1,-1};
            apply_redirs_in_parent(&cmd->redir,&saved);
            jobs_print_active();  // print background jobs
            restore_redirs_in_parent(&saved);
            return 0;
        }

        // external command
        char *abs = lookup_in_path(argv[0]);
        if (!abs) { fprintf(stderr,"exec failed: %s: No such file\n",argv[0]); return -1; }

        exec_opts_t opts={.in_fd=-1,.out_fd=-1,.err_fd=-1,.background=pl->background?true:false};
        if (apply_redir_paths(&cmd->redir,&opts)<0) { perror("redir"); free(abs); return -1; }

        pid_t child=-1; int status=0;
        int rc = run_command(abs, cmd->argv, &opts, &child, &status);
        if (opts.in_fd!=-1) close(opts.in_fd);
        if (opts.out_fd!=-1) close(opts.out_fd);
        free(abs);
        if (rc<0) return -1;
        return pl->background ? 0 : status;
    }

    // multi-stage pipelines: optional
    fprintf(stderr,"[background execution not implemented yet]\n");
    return 0;
}
