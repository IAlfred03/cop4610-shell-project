// Person B: Pipeline execution implementation
// Uses Person A's run_command() function for launching individual commands
// Person B tests: builds a pipeline from a command line and runs exec_pipeline()
// Requires: parser.h/.c, builtins.h/.c, exec.h (with exec_pipeline + run_command)
#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static long fsize(const char *p){
    struct stat st; if (stat(p, &st) != 0) return -1; return (long)st.st_size;
}

static int file_eq(const char *p, const char *want){
    FILE *f = fopen(p, "rb"); if(!f) return 0;
    char buf[4096]; size_t n = fread(buf,1,sizeof(buf),f); fclose(f);
    return (int)(n == strlen(want) && memcmp(buf, want, n) == 0);
}

static void ensure_tmp(void){
    struct stat st;
    if (stat("tests/tmp", &st) != 0) {
        mkdir("tests/tmp", 0777);
    }
}

static int run_line(const char *line) {
    pipeline_t pl = (pipeline_t){0};
    if (parse_line(line, &pl) != 0) {
        fprintf(stderr, "[PARSE-ERR] %s\n", line);
        return 1;
    }
    int rc = exec_pipeline(&pl);
    free_pipeline(&pl);
    return rc;
}

/* Absolute paths (macOS defaults). Adjust if needed on your system. */
static const char *PRINTF = "/usr/bin/printf";
static const char *CAT    = "/bin/cat";
static const char *GREP   = "/usr/bin/grep";
static const char *SORT   = "/usr/bin/sort";
static const char *WC     = "/usr/bin/wc";
static const char *SLEEP  = "/bin/sleep";

static int test_basic_echo(void){
    ensure_tmp();
    char line[512];
    // Portable: printf "%s\n" "hi"
    snprintf(line, sizeof(line),
             "%s \"%%s\\n\" \"hi\" > tests/tmp/basic.txt", PRINTF);
    if (run_line(line) != 0) return 1;
    if (!file_eq("tests/tmp/basic.txt", "hi\n")) return 1;
    return 0;
}

static int test_redirs_append(void){
    ensure_tmp();
    char line1[512], line2[512];
    // Append with deterministic newlines
    snprintf(line1, sizeof(line1),
             "%s \"%%s\\n\" \"one\" > tests/tmp/redir.txt", PRINTF);
    snprintf(line2, sizeof(line2),
             "%s \"%%s\\n\" \"two\" >> tests/tmp/redir.txt", PRINTF);
    if (run_line(line1) != 0) return 1;
    if (run_line(line2) != 0) return 1;
    if (!file_eq("tests/tmp/redir.txt", "one\ntwo\n")) return 1;
    return 0;
}

static int test_pipeline_3stage(void){
    ensure_tmp();
    // Prepare input
    FILE *f = fopen("tests/tmp/in.txt","wb");
    if(!f) return 1;
    fputs("b\nx\na\nbb\n", f);
    fclose(f);

    // cat < in | grep b | sort > out
    char line[512];
    snprintf(line, sizeof(line),
             "%s < tests/tmp/in.txt | %s b | %s > tests/tmp/out.txt",
             CAT, GREP, SORT);
    if (run_line(line) != 0) return 1;

    FILE *g = fopen("tests/tmp/out.txt","rb");
    if(!g) return 1;
    char buf[64]; size_t n=fread(buf,1,sizeof(buf),g); fclose(g);
    if (n != 5 || memcmp(buf, "b\nbb\n", 5) != 0) return 1;
    return 0;
}

static int test_builtin_cd_pwd(void){
    char orig[4096];
    if (!getcwd(orig, sizeof(orig))) return 1;

    if (run_line("cd /") != 0) return 1;
    char cwd[4096]; if(!getcwd(cwd,sizeof(cwd))) return 1;
    int ok = (cwd[0] == '/');

    /* IMPORTANT: restore working directory so later tests use project-relative paths */
    int rc = chdir(orig);
    (void)rc;
    return ok ? 0 : 1;
}

static int test_builtin_in_pipeline(void){
    ensure_tmp();
    // pwd | wc -c > out  (builtin in a pipeline runs in a child)
    char line[512];
    snprintf(line, sizeof(line),
             "pwd | %s -c > tests/tmp/pwdlen.txt", WC);
    if (run_line(line) != 0) return 1;
    if (fsize("tests/tmp/pwdlen.txt") <= 0) return 1;
    return 0;
}

static int test_background_returns(void){
    char line[512];
    snprintf(line, sizeof(line), "%s 1 &", SLEEP);
    if (run_line(line) != 0) return 1;
    return 0;
}

static int test_in_redir_and_wc(void){
    ensure_tmp();
    FILE *f = fopen("tests/tmp/bytes.txt","wb");
    if(!f) return 1;
    fputs("hello", f); fclose(f);

    // cat < bytes.txt | wc -c > count.txt   => should be 5 bytes
    char line[512];
    snprintf(line, sizeof(line),
             "%s < tests/tmp/bytes.txt | %s -c > tests/tmp/count.txt", CAT, WC);
    if (run_line(line) != 0) return 1;

    FILE *g = fopen("tests/tmp/count.txt","rb");
    if(!g) return 1;
    char buf[32]; size_t n = fread(buf,1,sizeof(buf),g); fclose(g);
    if (n == 0 || n >= sizeof(buf)) return 1;
    buf[n] = '\0';

    // Skip possible leading spaces/tabs printed by wc
    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;

    // Parse integer and verify == 5
    char *end = NULL;
    long val = strtol(p, &end, 10);
    if (end == p) return 1; // no number parsed
    return (val == 5) ? 0 : 1;
}

int main(void){
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"basic_echo",            test_basic_echo},
        {"redirs_append",         test_redirs_append},
        {"pipeline_3stage",       test_pipeline_3stage},
        {"builtin_cd_pwd",        test_builtin_cd_pwd},
        {"builtin_in_pipeline",   test_builtin_in_pipeline},
        {"background_returns",    test_background_returns},
        {"in_redir_and_wc",       test_in_redir_and_wc},
    };

    int fails = 0;
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
        int rc = tests[i].fn();
        printf("[%-20s] %s\n", tests[i].name, rc==0 ? "PASS" : "FAIL");
        fails += (rc != 0);
    }
    if (fails) {
        fprintf(stderr, "\n%d test(s) failed.\n", fails);
        return 1;
    }
    puts("\nAll Person-B tests passed.");
    return 0;
}
