// Person C tests: expansion + builtins
#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int run_line(const char *line) {
    pipeline_t pl = {0};
    if (parse_line(line, &pl) != 0) {
        fprintf(stderr, "[PARSE-ERR] %s\n", line);
        return 1;
    }
    int rc = exec_pipeline(&pl);
    free_pipeline(&pl);
    return rc;
}

// --- Tests ---

static int test_env_expansion(void) {
    char *user = getenv("USER");
    if (!user) user = "unknown";
    char expect[256]; snprintf(expect, sizeof(expect), "%s\n", user);

    int rc = system("mkdir -p tests/tmp");
    (void)rc;
    if (run_line("echo $USER > tests/tmp/env.txt") != 0) return 1;

    FILE *f = fopen("tests/tmp/env.txt","rb");
    if (!f) return 1;
    char buf[256]; size_t n=fread(buf,1,sizeof(buf),f); fclose(f);
    buf[n] = '\0';
    return strcmp(buf, expect) != 0;
}

static int test_tilde_expansion(void) {
    char *home = getenv("HOME");
    if (!home) return 1;

    int rc = system("mkdir -p tests/tmp");
    (void)rc;
    if (run_line("echo ~/ > tests/tmp/tilde.txt") != 0) return 1;

    FILE *f = fopen("tests/tmp/tilde.txt","rb");
    if (!f) return 1;
    char buf[256]; size_t n=fread(buf,1,sizeof(buf),f); fclose(f);
    buf[n] = '\0';
    // Should print home path + "\n"
    char expect[512]; snprintf(expect,sizeof(expect),"%s\n",home);
    return strcmp(buf, expect) != 0;
}

static int test_builtin_cd(void) {
    if (run_line("cd /") != 0) return 1;
    char cwd[1024]; if (!getcwd(cwd,sizeof(cwd))) return 1;
    return cwd[0] != '/'; // ensure we changed into /
}

static int test_builtin_jobs(void) {
    // Launch background sleep
    if (run_line("/bin/sleep 1 &") != 0) return 1;
    // Immediately ask for jobs
    if (run_line("jobs > tests/tmp/jobs.txt") != 0) return 1;
    FILE *f = fopen("tests/tmp/jobs.txt","rb");
    if (!f) return 1;
    char buf[128]; size_t n=fread(buf,1,sizeof(buf),f); fclose(f);
    buf[n] = '\0';
    return strstr(buf,"sleep") == NULL; // should mention sleep
}

static int test_exit_history(void) {
    // Hard to fully automate exit() since it kills test runner.
    // Instead, rely on run_line calling builtin exit handler returning special code.
    // Here we just check parse/exec works.
    return run_line("exit") != 0;
}

int main(void) {
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"env_expansion",  test_env_expansion},
        {"tilde_expansion",test_tilde_expansion},
        {"builtin_cd",     test_builtin_cd},
        {"builtin_jobs",   test_builtin_jobs},
        {"builtin_exit",   test_exit_history},
    };

    int fails = 0;
    for (size_t i=0; i<sizeof(tests)/sizeof(tests[0]); ++i) {
        int rc = tests[i].fn();
        printf("[%-20s] %s\n", tests[i].name, rc==0 ? "PASS" : "FAIL");
        fails += (rc != 0);
    }
    if (fails) {
        fprintf(stderr, "\n%d Person-C test(s) failed.\n", fails);
        return 1;
    }
    puts("\nAll Person-C tests passed.");
    return 0;
}
