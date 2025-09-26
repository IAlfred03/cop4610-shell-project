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

static long fsize(const char *p){
    struct stat st; if (stat(p, &st) != 0) return -1; return (long)st.st_size;
}

static int file_eq(const char *p, const char *want){
    FILE *f = fopen(p, "rb"); if(!f) return 0;
    char buf[4096]; size_t n = fread(buf,1,sizeof(buf),f); fclose(f);
    return (int)(n == strlen(want) && memcmp(buf, want, n) == 0);
}

// Adjust these if your system differs
static const char *ECHO = "/bin/echo";
static const char *CAT  = "/bin/cat";
static const char *GREP = "/usr/bin/grep";
static const char *SORT = "/usr/bin/sort";
static const char *WC   = "/usr/bin/wc";
static const char *SLEEP= "/bin/sleep";

static void ensure_tmp(void){ mkdir("tests/tmp", 0777); }

static int test_basic_echo(void){
    // stdout redirection to a file so we can assert
    ensure_tmp();
    char line[512];
    snprintf(line, sizeof(line), "%s hi > tests/tmp/basic.txt", ECHO);
    if (run_line(line) != 0) return 1;
    if (!file_eq("tests/tmp/basic.txt", "hi\n")) return 1;
    return 0;
}

static int test_redirs_append(void){
    ensure_tmp();
    char line1[512], line2[512];
    snprintf(line1, sizeof(line1), "%s one > tests/tmp/redir.txt", ECHO);
    snprintf(line2, sizeof(line2), "%s two >> tests/tmp/redir.txt", ECHO);
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

    // out should contain: b\nbb\n
    FILE *g = fopen("tests/tmp/out.txt","rb");
    if(!g) return 1;
    char buf[64]; size_t n=fread(buf,1,sizeof(buf),g); fclose(g);
    if (n != 6 || memcmp(buf, "b\nbb\n", 6) != 0) return 1;
    return 0;
}

static int test_builtin_cd_pwd(void){
    // Single-stage builtin in parent; change to / then confirm with getcwd
    if (run_line("cd /") != 0) return 1;
    char cwd[4096]; if(!getcwd(cwd,sizeof(cwd))) return 1;
    // Must start with "/"
    if (cwd[0] != '/') return 1;
    return 0;
}

static int test_builtin_in_pipeline(void){
    ensure_tmp();
    // pwd | wc -c > out  (builtin in a pipeline should run in child)
    char line[512];
    snprintf(line, sizeof(line), "pwd | %s -c > tests/tmp/pwdlen.txt", WC);
    if (run_line(line) != 0) return 1;
    if (fsize("tests/tmp/pwdlen.txt") <= 0) return 1;
    return 0;
}

static int test_background_returns(void){
    // Should return immediately (we can't time precisely; just ensure it doesn't hang)
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

    // cat < bytes.txt | wc -c > count.txt   => "5\n"
    char line[512];
    snprintf(line, sizeof(line),
             "%s < tests/tmp/bytes.txt | %s -c > tests/tmp/count.txt", CAT, WC);
    if (run_line(line) != 0) return 1;

    FILE *g = fopen("tests/tmp/count.txt","rb");
    if(!g) return 1;
    char buf[16]; size_t n=fread(buf,1,sizeof(buf),g); fclose(g);
    return !(n==2 && buf[0]=='5' && buf[1]=='\n'); // 0 = pass, 1 = fail
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
