#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "../include/prompt.h"
#include "../include/exec.h"
#include "../include/jobs.h"

static void rstrip(char *s){
    if (!s) return;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}
static int ends_with_ampersand(char *s){
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) n--;
    if (n && s[n-1] == '&'){ s[n-1] = '\0'; rstrip(s); return 1; }
    return 0;
}
static char **split_simple(const char *line){
    // very simple whitespace split for testing; no quotes or escapes
    size_t cap = 8, n = 0;
    char **argv = malloc(sizeof(char*) * cap);
    char *tmp = strdup(line);
    char *tok = strtok(tmp, " \t");
    while (tok){
        if (n+1 >= cap){ cap *= 2; argv = realloc(argv, sizeof(char*) * cap); }
        argv[n++] = strdup(tok);
        tok = strtok(NULL, " \t");
    }
    argv[n] = NULL;
    free(tmp);
    return argv;
}
static void free_argv(char **argv){
    if (!argv) return;
    for (size_t i=0; argv[i]; ++i) free(argv[i]);
    free(argv);
}

int main(void){
    jobs_init();

    fprintf(stderr,
        "A-sanity harness (Person A only)\n"
        "Use absolute paths (e.g., /bin/echo hi, /bin/sleep 1 &)\n"
        "Built-ins supported here: jobs, exit\n\n");

    for (;;) {
        show_prompt();

        char *line = NULL; size_t cap = 0;
        ssize_t nr = getline(&line, &cap, stdin);
        if (nr < 0){ putchar('\n'); jobs_wait_all(); free(line); return 0; }
        rstrip(line);
        if (!*line){ free(line); jobs_mark_done_nonblocking(); continue; }

        if (strcmp(line, "exit") == 0){
            jobs_wait_all();
            free(line);
            return 0;
        }
        if (strcmp(line, "jobs") == 0){
            jobs_print_active();
            free(line);
            jobs_mark_done_nonblocking();
            continue;
        }

        int background = ends_with_ampersand(line);
        char **argv = split_simple(line);
        const char *cmd = argv[0];

        if (!cmd || !*cmd){
            fprintf(stderr, "empty command\n");
            free_argv(argv); free(line);
            jobs_mark_done_nonblocking();
            continue;
        }
        if (!strchr(cmd, '/')){
            fprintf(stderr, "for this test, use an absolute path (contains '/')\n");
            free_argv(argv); free(line);
            jobs_mark_done_nonblocking();
            continue;
        }

        exec_opts_t opts = {.in_fd=-1, .out_fd=-1, .err_fd=-1, .background=background};
        pid_t pid = -1; int status = 0;

        if (run_command(cmd, argv, &opts, &pid, &status) == 0){
            if (background && pid > 0){
                int job_id = jobs_next_id();
                jobs_register(job_id, pid, line);
            }
        }

        free_argv(argv);
        free(line);

        jobs_mark_done_nonblocking();
    }
}
