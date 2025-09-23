// Person A: REPL skeleton wiring prompt + exec + jobs.
// Person B/C: implement the headers below and their corresponding .c files.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../include/prompt.h"   // A
#include "../include/exec.h"     // A
#include "../include/jobs.h"     // A
#include "../include/myparser.h"     // C

/* ---- Person C: provide these ---- */
// parser.h
//  - parse_line(const char*, cmd_plan_t*)
//  - free_plan(cmd_plan_t*)
//  - cmd_plan_t contains:
//      plan.kind: PLAN_SINGLE or PLAN_PIPELINE
//      plan.background: 0/1
//      plan.is_builtin: 0/1
//      plan.which: enum { BI_NONE, BI_EXIT, BI_CD, BI_JOBS }
//      plan.original: original command line (<= 200 chars)
//      plan.single.argv: NULL-terminated argv for single command
//      plan.pipe.argvv: array of argv*; plan.pipe.ncmds in {2,3}
//#include "parser.h"

// builtins.h
//  - builtin_execute(const cmd_plan_t *plan)
//    returns 1 if built-in is "exit" and shell should quit (after A calls jobs_wait_all())

//#include "builtins.h"

/* ---- Person B: provide these ---- */
// path.h
//  - path_resolve(const char *cmd, char *abs_path, size_t abs_n)
//    return 0 on success, -1 if not found in $PATH
//#include "path.h"

// pipeline.h
//  - pipeline_run(const cmd_plan_t *plan, int background, pid_t *last_pid)
//    runs 2–3 stage pipeline. If background: do not wait; set *last_pid to last stage PID.
//    If foreground: wait internally and return 0 when done.
//#include "pipeline.h"

/* helpers */
static void rstrip(char *s){
    if (!s) return;
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || isspace((unsigned char)s[n-1]))) s[--n] = '\0';
}
static int is_blank(const char *s){
    if (!s) return 1;
    for (; *s; ++s) if (!isspace((unsigned char)*s)) return 0;
    return 1;
}

int main(void){
    jobs_init(); // A: background job system

    for (;;) {
        show_prompt(); // A

        char *line = NULL;
        size_t cap = 0;
        ssize_t nread = getline(&line, &cap, stdin);
        if (nread == -1){
            putchar('\n');
            jobs_wait_all(); // A: ensure bg tasks finish
            free(line);
            return 0;
        }
        rstrip(line);
        if (is_blank(line)){
            free(line);
            jobs_mark_done_nonblocking(); // A: reap finished bg jobs each tick
            continue;
        }

        /* ---- Person C: parse & expand ---- */
        cmd_plan_t plan;
        memset(&plan, 0, sizeof(plan));
        if (!parse_line(line, &plan)){
            fprintf(stderr, "parse error\n");
            free(line);
            jobs_mark_done_nonblocking();
            continue;
        }
        if (plan.original[0] == '\0')
            snprintf(plan.original, sizeof(plan.original), "%s", line);

        /* ---- Person C: built-ins ---- */
        if (plan.is_builtin){
            int should_quit = builtin_execute(&plan);
            free_plan(&plan);
            free(line);
            if (should_quit){
                jobs_wait_all();       // A: required by spec
                return 0;              // C prints history before returning true
            }
            jobs_mark_done_nonblocking(); // A
            continue;
        }

        /* ---- Person A/B: external single or pipeline ---- */
        int bg = plan.background ? 1 : 0;

        if (plan.kind == PLAN_SINGLE){
            const char *cmd = plan.single.argv && plan.single.argv[0] ? plan.single.argv[0] : NULL;
            if (!cmd || !*cmd){
                fprintf(stderr, "empty command\n");
            } else {
                char abs_path[512] = {0};
                int need_resolve = strchr(cmd, '/') == NULL;
                if (need_resolve){
                    /* Person B: resolve via $PATH into abs_path. */
                    if (path_resolve(cmd, abs_path, sizeof(abs_path)) != 0){
                        fprintf(stderr, "%s: command not found\n", cmd);
                    } else {
                        exec_opts_t opts = {.in_fd=-1, .out_fd=-1, .err_fd=-1, .background=bg};
                        pid_t child=-1; int status=0;
                        if (run_command(abs_path, plan.single.argv, &opts, &child, &status) == 0 && bg){
                            int job_id = jobs_next_id();
                            jobs_register(job_id, child, plan.original); // A
                        }
                    }
                } else {
                    /* cmd has '/', run directly. */
                    exec_opts_t opts = {.in_fd=-1, .out_fd=-1, .err_fd=-1, .background=bg};
                    pid_t child=-1; int status=0;
                    if (run_command(cmd, plan.single.argv, &opts, &child, &status) == 0 && bg){
                        int job_id = jobs_next_id();
                        jobs_register(job_id, child, plan.original); // A
                    }
                }
            }
        } else if (plan.kind == PLAN_PIPELINE){
            /* Person B: run pipeline; return last stage PID for background registration. */
            pid_t last_pid = -1;
            if (pipeline_run(&plan, bg, &last_pid) == 0 && bg && last_pid > 0){
                int job_id = jobs_next_id();
                jobs_register(job_id, last_pid, plan.original); // A
            }
        } else {
            fprintf(stderr, "unsupported plan kind\n");
        }

        jobs_mark_done_nonblocking(); // A: reap finished bg jobs
        free_plan(&plan);             // C
        free(line);
    }
}
