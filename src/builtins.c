#define _POSIX_C_SOURCE 200809L
#include "builtins.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// --- cd --------------------------------------------------------------------
static int bi_cd(char *const argv[]) {
    const char *path = argv[1] ? argv[1] : getenv("HOME");
    if (!path) {
        fprintf(stderr, "cd: HOME not set\n");
        return 1;
    }
    if (chdir(path) < 0) {
        perror("cd");
        return 1;
    }
    return 0;
}

static int bi_pwd(char *const argv[]) {
    (void)argv;  // unused
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) {
        perror("pwd");
        return 1;
    }
    printf("%s\n", buf);
    fflush(stdout);
    return 0;
}

static int bi_exit(char *const argv[]) {
    int code = 0;
    if (argv[1]) {
        code = atoi(argv[1]);
    }
    return 2001 + (code & 0xFF);
}

bool is_builtin(const char *cmd) {
    if (!cmd) return false;
    return strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "exit") == 0;
}

int run_builtin_parent(char *const argv[]) {
    if (!argv || !argv[0]) return 0;

    if (strcmp(argv[0], "cd") == 0)   return bi_cd(argv);
    if (strcmp(argv[0], "pwd") == 0)  return bi_pwd(argv);
    if (strcmp(argv[0], "exit") == 0) return bi_exit(argv);

    // Not a builtin—should not get here if caller checks is_builtin().
    return 127;
}