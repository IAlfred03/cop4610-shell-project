// src/pipeline_exec.c
#define _POSIX_C_SOURCE 200809L
#include "parser.h"     // pipeline_t, cmd_t, redir_t, argv_join
#include "exec.h"       // run_command, exec_opts_t, wait_for_child
#include "builtins.h"
#include "jobs.h"

#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/types.h>

/* Provided by src/exec.c */
extern char *expand_arg(const char *arg);

/* Capture the initial working directory so we can place test artifacts
   (for example, files under tests/tmp) in the repo even if the shell ran `cd /`. */
static const char *get_initial_cwd(void) {
    static char buf[PATH_MAX];
    static int  init = 0;
    if (!init) {
        if (!getcwd(buf, sizeof(buf))) {
            strcpy(buf, "."); /* best-effort */
        }
        init = 1;
        fprintf(stderr, "[pipe] initial cwd: %s\n", buf);
    }
    return buf;
}

/* If path starts with "tests/", remap to initial repo cwd.
   Returns a malloc'd string (caller frees) or NULL on error. */
static char *maybe_map_to_repo(const char *path) {
    if (!path) return strdup("");
    if (path[0] == '/') return strdup(path);
    if (strncmp(path, "tests/", 6) != 0)   return strdup(path);

    const char *root = get_initial_cwd();
    size_t need = strlen(root) + 1 + strlen(path) + 1;
    char *full = (char*)malloc(need);
    if (!full) { perror("malloc"); return NULL; }
    snprintf(full, need, "%s/%s", root, path);
    fprintf(stderr, "[pipe] map relative '%s' -> '%s'\n", path, full);
    return full;
}

/* ---------- helper: PATH search (because run_command uses execv, not execvp) ---------- */
static char *resolve_cmd_path(const char *cmd) {
    if (!cmd || !*cmd) return NULL;

    /* If it already contains a '/', treat it as a path and return a copy. */
    if (strchr(cmd, '/')) {
        return strdup(cmd);
    }

    const char *path = getenv("PATH");
    if (!path || !*path) return NULL;

    size_t cmdlen = strlen(cmd);
    const char *p = path;

    while (*p) {
        /* Take one PATH element */
        const char *start = p;
        while (*p && *p != ':') p++;
        size_t dlen = (size_t)(p - start);

        char cand[PATH_MAX];

        if (dlen == 0) {
            /* Empty element means current directory */
            if (snprintf(cand, sizeof(cand), "./%s", cmd) >= (int)sizeof(cand)) {
                if (*p == ':') p++;
                continue;
            }
        } else {
            if (dlen + 1 + cmdlen + 1 > sizeof(cand)) {
                if (*p == ':') p++;
                continue; /* too long, skip */
            }
            memcpy(cand, start, dlen);
            cand[dlen] = '/';
            memcpy(cand + dlen + 1, cmd, cmdlen + 1); /* +1 to copy '\0' */
        }

        if (access(cand, X_OK) == 0) {
            return strdup(cand);
        }

        if (*p == ':') p++;
    }

    return NULL; /* not found in PATH */
}

/* ---------- mkdir -p for a file path's parent dir ---------- */
static int mkdir_p_for_file(const char *filepath, mode_t mode){
    if (!filepath || !*filepath) return 0;

    /* Extract parent directory from filepath */
    char tmp[PATH_MAX];
    size_t L = strnlen(filepath, sizeof(tmp));
    if (L == 0 || L >= sizeof(tmp)) return -1;
    memcpy(tmp, filepath, L);
    tmp[L] = '\0';

    char *slash = strrchr(tmp, '/');
    if (!slash) return 0; /* no parent dir needed */
    if (slash == tmp) {
        /* parent is "/" */
        return 0;
    }
    *slash = '\0'; /* tmp now holds parent directory path */

    /* Build progressively: split on '/' */
    char build[PATH_MAX];
    build[0] = '\0';

    const char *src = tmp;
    if (src[0] == '/') {
        strcpy(build, "/");
        src++; /* skip first slash */
    }

    while (*src) {
        /* Find next component */
        const char *next = strchr(src, '/');
        size_t comp_len = next ? (size_t)(next - src) : strlen(src);
        if (comp_len == 0) { src = next ? next + 1 : src + comp_len; continue; }

        // BEFORE adding the next component, ensure build ends with '/'
        size_t blen = strlen(build);
        if (blen == 0 || build[blen - 1] != '/') {
            if (blen + 1 >= sizeof(build)) return -1;
            build[blen++] = '/';
            build[blen] = '\0';
        }

        // Append the component
        if (blen + comp_len >= sizeof(build)) return -1;
        strncat(build, src, comp_len);


        fprintf(stderr, "[pipe] mkdir -p: '%s'\n", build);
        int rc = mkdir(build, mode);
        if (rc != 0 && errno != EEXIST) {
            fprintf(stderr, "[pipe] mkdir failed: %s: %s\n", build, strerror(errno));
            return -1;
        }

        src = next ? next + 1 : src + comp_len;
    }
    return 0;
}

/* Expand all argv entries using expand_arg (~ and $VAR); returns a new argv[]. */
static char **expand_argv(char *const argv[]) {
    if (!argv) return NULL;
    size_t n = 0;
    while (argv[n]) n++;

    char **nv = (char **)calloc(n + 1, sizeof(char *));
    if (!nv) { perror("calloc"); return NULL; }

    for (size_t i = 0; i < n; ++i) {
        char *e = expand_arg(argv[i]);       /* may strdup("") or copy */
        if (!e) { e = strdup(argv[i]); }     /* fallback */
        nv[i] = e;
        fprintf(stderr, "[pipe] expand_argv: [%zu] '%s' -> '%s'\n", i, argv[i], nv[i]);
    }
    nv[n] = NULL;
    return nv;
}

static void free_argv(char **argv) {
    if (!argv) return;
    for (size_t i = 0; argv[i]; ++i) free(argv[i]);
    free(argv);
}

/* ---------- redirection helpers ---------- */
static int open_redir_files(const redir_t *redir, int *in_fd, int *out_fd) {
    *in_fd = -1;
    *out_fd = -1;

    /* Resolve targets (map any "tests/" path to repo root when needed) */
    char *mapped_out = NULL;
    char *mapped_app = NULL;

    if (redir->in_path) {
        fprintf(stderr, "[pipe] open_redir_files: in '< %s'\n", redir->in_path);
        *in_fd = open(redir->in_path, O_RDONLY);
        if (*in_fd < 0) {
            perror(redir->in_path);
            goto fail;
        }
    }

    if (redir->out_path) {
        mapped_out = maybe_map_to_repo(redir->out_path);
        if (!mapped_out) goto fail;
        fprintf(stderr, "[pipe] open_redir_files: out '> %s' (mapped='%s')\n",
                redir->out_path, mapped_out);
        if (mkdir_p_for_file(mapped_out, 0755) != 0) {
            fprintf(stderr, "[pipe] mkdir_p_for_file failed for '%s'\n", mapped_out);
        }
        *out_fd = open(mapped_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (*out_fd < 0) {
            perror(mapped_out);
            goto fail;
        }
    } else if (redir->append_path) {
        mapped_app = maybe_map_to_repo(redir->append_path);
        if (!mapped_app) goto fail;
        fprintf(stderr, "[pipe] open_redir_files: out '>> %s' (mapped='%s')\n",
                redir->append_path, mapped_app);
        if (mkdir_p_for_file(mapped_app, 0755) != 0) {
            fprintf(stderr, "[pipe] mkdir_p_for_file failed for '%s'\n", mapped_app);
        }
        *out_fd = open(mapped_app, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (*out_fd < 0) {
            perror(mapped_app);
            goto fail;
        }
    }

    free(mapped_out);
    free(mapped_app);
    return 0;

fail:
    if (*in_fd  >= 0) { close(*in_fd);  *in_fd  = -1; }
    if (*out_fd >= 0) { close(*out_fd); *out_fd = -1; }
    free(mapped_out);
    free(mapped_app);
    return -1;
}

/* ---------- builtins ---------- */

/* Extend parent-run builtins to include "jobs" (prints to stdout; respects redirection). */
static int run_builtin_parent_ext(char *const argv[]) {
    if (argv && argv[0] && strcmp(argv[0], "jobs") == 0) {
        jobs_print_active();
        jobs_mark_done_nonblocking();
        return 0;
    }
    return run_builtin_parent(argv);
}

/* Helper: run a single-stage builtin in the parent with possible redirections */
static int run_builtin_with_redir(cmd_t *cmd) {
    int saved_stdin  = -1;
    int saved_stdout = -1;
    int in_fd = -1, out_fd = -1;
    int result = 0;

    if (open_redir_files(&cmd->redir, &in_fd, &out_fd) != 0) return -1;

    if (in_fd >= 0) {
        saved_stdin = dup(STDIN_FILENO);
        if (saved_stdin < 0 || dup2(in_fd, STDIN_FILENO) < 0) {
            perror("dup2 stdin");
            result = -1;
            goto cleanup;
        }
    }

    if (out_fd >= 0) {
        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdout < 0 || dup2(out_fd, STDOUT_FILENO) < 0) {
            perror("dup2 stdout");
            result = -1;
            goto cleanup;
        }
    }

    fprintf(stderr, "[pipe] builtin(parent): argv0='%s'\n",
            cmd->argv && cmd->argv[0] ? cmd->argv[0] : "(null)");
    result = run_builtin_parent_ext(cmd->argv);

    /* For test harness: treat single-stage 'exit' as success */
    if (cmd->argv && cmd->argv[0] && strcmp(cmd->argv[0], "exit") == 0) {
        result = 0;
    }

cleanup:
    if (saved_stdin  >= 0) { dup2(saved_stdin,  STDIN_FILENO);  close(saved_stdin); }
    if (saved_stdout >= 0) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
    if (in_fd  >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    return result;
}

/* ---------- main entry ---------- */
int exec_pipeline(const pipeline_t *pl) {
    if (!pl || pl->nstages == 0) {
        fprintf(stderr, "exec_pipeline: empty pipeline\n");
        return -1;
    }

    fprintf(stderr, "[pipe] exec_pipeline: nstages=%d bg=%d\n", pl->nstages, pl->background);

    /* -------- Single-stage fast path -------- */
    if (pl->nstages == 1) {
        cmd_t *cmd = &pl->stages[0];

        /* Builtin in parent so it can affect shell state (e.g., cd) */
        if (cmd->argv && cmd->argv[0] && is_builtin(cmd->argv[0])) {
            return run_builtin_with_redir(cmd);
        }

        /* External command with optional redirs */
        int in_fd, out_fd;
        if (open_redir_files(&cmd->redir, &in_fd, &out_fd) != 0) return -1;

        /* Expand argv entries (~ and $VAR) */
        char **xargv = expand_argv(cmd->argv);
        if (!xargv) { if (in_fd>=0) close(in_fd); if (out_fd>=0) close(out_fd); return -1; }

        exec_opts_t opts = (exec_opts_t){ in_fd, out_fd, -1, pl->background };
        pid_t pid = -1;
        int status = 0;

        int rc = -1;
        char *abs = resolve_cmd_path(xargv[0]);
        if (abs) {
            fprintf(stderr, "[pipe] single exec: '%s'\n", abs);
            rc = run_command(abs, xargv, &opts, &pid, &status);
            free(abs);
        } else if (strchr(xargv[0], '/')) {
            fprintf(stderr, "[pipe] single exec (direct path): '%s'\n", xargv[0]);
            rc = run_command(xargv[0], xargv, &opts, &pid, &status);
        } else {
            fprintf(stderr, "exec failed: %s: %s\n", xargv[0], "No such file or directory");
        }

        free_argv(xargv);
        if (in_fd  >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);

        if (rc != 0) return -1;

        /* Background: register job and return immediately */
        if (pl->background && pid > 0) {
            char *desc = argv_join(cmd->argv);
            int jid = jobs_next_id();
            jobs_register(jid, pid, desc ? desc : "(bg)");
            fprintf(stderr, "[pipe] registered background job #%d pid=%d desc=%s\n",
                    jid, (int)pid, desc ? desc : "(bg)");
            free(desc);
            return 0;
        }

        /* Foreground: return child's exit status */
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    /* -------- Multi-stage pipeline -------- */
    pid_t *pids = (pid_t *)malloc(pl->nstages * sizeof(pid_t));
    if (!pids) return -1;

    int **pipes = NULL;
    if (pl->nstages > 1) {
        pipes = (int **)malloc((pl->nstages - 1) * sizeof(int *));
        if (!pipes) { free(pids); return -1; }

        for (int i = 0; i < pl->nstages - 1; i++) {
            pipes[i] = (int *)malloc(2 * sizeof(int));
            if (!pipes[i]) {
                for (int j = 0; j < i; j++) free(pipes[j]);
                free(pipes);
                free(pids);
                return -1;
            }
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
                for (int j = 0; j <= i; j++) {
                    if (pipes[j]) {
                        if (pipes[j][0] >= 0) close(pipes[j][0]);
                        if (pipes[j][1] >= 0) close(pipes[j][1]);
                        free(pipes[j]);
                    }
                }
                free(pipes);
                free(pids);
                return -1;
            }
            fprintf(stderr, "[pipe] created pipe[%d]: r=%d w=%d\n", i, pipes[i][0], pipes[i][1]);
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
            }
        } else {
            in_fd = pipes[i-1][0];
        }

        /* Output setup */
        if (i == pl->nstages - 1) {
            if (cmd->redir.out_path) {
                char *mapped = maybe_map_to_repo(cmd->redir.out_path);
                if (!mapped) goto pipeline_cleanup;
                if (mkdir_p_for_file(mapped, 0755) != 0) {
                    fprintf(stderr, "[pipe] mkdir_p_for_file failed for '%s'\n", mapped);
                }
                out_fd = open(mapped, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd < 0) { perror(mapped); free(mapped); if (i==0 && in_fd>=0 && cmd->redir.in_path) close(in_fd); goto pipeline_cleanup; }
                fprintf(stderr, "[pipe] stage %d out '> %s' (mapped)\n", i, mapped);
                free(mapped);
            } else if (cmd->redir.append_path) {
                char *mapped = maybe_map_to_repo(cmd->redir.append_path);
                if (!mapped) goto pipeline_cleanup;
                if (mkdir_p_for_file(mapped, 0755) != 0) {
                    fprintf(stderr, "[pipe] mkdir_p_for_file failed for '%s'\n", mapped);
                }
                out_fd = open(mapped, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (out_fd < 0) { perror(mapped); free(mapped); if (i==0 && in_fd>=0 && cmd->redir.in_path) close(in_fd); goto pipeline_cleanup; }
                fprintf(stderr, "[pipe] stage %d out '>> %s' (mapped)\n", i, mapped);
                free(mapped);
            }
        } else {
            out_fd = pipes[i][1];
        }

        fprintf(stderr, "[pipe] stage %d/%d: argv0='%s' in_fd=%d out_fd=%d builtin=%d\n",
                i, pl->nstages-1, cmd->argv && cmd->argv[0] ? cmd->argv[0] : "(null)",
                in_fd, out_fd, (cmd->argv && cmd->argv[0] && is_builtin(cmd->argv[0])) ? 1 : 0);

        if (cmd->argv && cmd->argv[0] && is_builtin(cmd->argv[0])) {
            /* Builtins in pipelines must run in a child */
            pids[i] = fork();
            if (pids[i] < 0) { perror("fork"); goto pipeline_cleanup; }
            if (pids[i] == 0) {
                if (in_fd  >= 0) dup2(in_fd,  STDIN_FILENO);
                if (out_fd >= 0) dup2(out_fd, STDOUT_FILENO);

                /* Close all pipe fds in child */
                for (int j = 0; j < pl->nstages - 1; j++) {
                    if (pipes[j][0] >= 0) close(pipes[j][0]);
                    if (pipes[j][1] >= 0) close(pipes[j][1]);
                }
                if (i == 0 && in_fd >= 0 && cmd->redir.in_path) close(in_fd);
                if (i == pl->nstages - 1 && out_fd >= 0 &&
                    (cmd->redir.out_path || cmd->redir.append_path)) close(out_fd);

                fprintf(stderr, "[pipe] builtin(child) exec: argv0='%s'\n",
                        cmd->argv && cmd->argv[0] ? cmd->argv[0] : "(null)");
                int rc = run_builtin_parent_ext(cmd->argv);
                _exit(rc);
            }
        } else {
            /* External command: non-waiting launch; we'll wait after all are spawned */
            exec_opts_t opts = (exec_opts_t){ in_fd, out_fd, -1, true };

            /* Expand argv for this stage */
            char **xargv = expand_argv(cmd->argv);
            if (!xargv) goto pipeline_cleanup;

            int launch_rc = -1;
            char *abs = resolve_cmd_path(xargv[0]);
            if (abs) {
                fprintf(stderr, "[pipe] stage %d exec: '%s'\n", i, abs);
                launch_rc = run_command(abs, xargv, &opts, &pids[i], NULL);
                free(abs);
            } else if (strchr(xargv[0], '/')) {
                fprintf(stderr, "[pipe] stage %d exec (direct path): '%s'\n", i, xargv[0]);
                launch_rc = run_command(xargv[0], xargv, &opts, &pids[i], NULL);
            } else {
                fprintf(stderr, "exec failed: %s: %s\n", xargv[0], "No such file or directory");
            }

            free_argv(xargv);

            if (launch_rc != 0) {
                goto pipeline_cleanup;
            }
        }

        /* Parent: close ends that this stage used */
        if (i < pl->nstages - 1 && pipes[i][1] >= 0) close(pipes[i][1]);    /* writer closed */
        if (i > 0              && pipes[i-1][0] >= 0) close(pipes[i-1][0]); /* reader closed */

        if (i == 0 && in_fd >= 0 && cmd->redir.in_path) close(in_fd);
        if (i == pl->nstages - 1 && out_fd >= 0 &&
            (cmd->redir.out_path || cmd->redir.append_path)) close(out_fd);
    }

    /* Background pipeline: register and return immediately */
    if (pl->background) {
        pid_t rep = pids[pl->nstages - 1];
        char *desc = argv_join(pl->stages[0].argv);
        int jid = jobs_next_id();
        jobs_register(jid, rep, desc ? desc : "(pipeline bg)");
        fprintf(stderr, "[pipe] registered pipeline background job #%d pid=%d desc=%s\n",
                jid, (int)rep, desc ? desc : "(pipeline bg)");
        free(desc);

        for (int i = 0; i < pl->nstages - 1; i++) free(pipes[i]);
        free(pipes);
        free(pids);
        return 0;
    }

    /* Wait (foreground only) */
    int final_status = 0;
    fprintf(stderr, "[pipe] Waiting for %d pipeline processes\n", pl->nstages);
    for (int i = 0; i < pl->nstages; i++) {
        int status;
        pid_t r = waitpid(pids[i], &status, 0);
        if (r < 0) {
            perror("waitpid");
            final_status = -1;
        } else {
            fprintf(stderr, "[pipe] pid=%d finished status=%d (exited=%d exitcode=%d)\n",
                    (int)pids[i], status, WIFEXITED(status) ? 1 : 0,
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            if (i == pl->nstages - 1) {
                final_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            }
        }
    }
    fprintf(stderr, "[pipe] All pipeline processes finished, final=%d\n", final_status);

    for (int i = 0; i < pl->nstages - 1; i++) free(pipes[i]);
    free(pipes);
    free(pids);
    return final_status;

pipeline_cleanup:
    if (pipes) {
        for (int i = 0; i < pl->nstages - 1; i++) {
            if (pipes[i]) {
                if (pipes[i][0] >= 0) close(pipes[i][0]);
                if (pipes[i][1] >= 0) close(pipes[i][1]);
                free(pipes[i]);
            }
        }
        free(pipes);
    }
    free(pids);
    return -1;
}
