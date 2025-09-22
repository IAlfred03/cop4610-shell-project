#define _POSIX_C_SOURCE 200809L
#include "prompt.h"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * Prints: USER@MACHINE:PWD>
 *  - USER   from $USER (fallback to getlogin() or "user")
 *  - MACHINE from gethostname() or $HOSTNAME
 *  - PWD    from getcwd()
 */
void show_prompt(void){
    char host[256] = {0};
    char cwd[PATH_MAX] = {0};
    const char *user = getenv("USER");
    if (!user || !*user) {
        user = getlogin();
        if (!user) user = "user";
    }
    if (gethostname(host, sizeof(host)-1) != 0 || host[0] == '\0') {
        const char *h = getenv("HOSTNAME");
        if (h && *h) snprintf(host, sizeof(host), "%s", h);
        else snprintf(host, sizeof(host), "machine");
    }
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(cwd, sizeof(cwd), "/");
    }
    printf("%s@%s:%s> ", user, host, cwd);
    fflush(stdout);
}
