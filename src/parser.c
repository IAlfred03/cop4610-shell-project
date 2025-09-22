#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void rstrip(char *s){
    if(!s) return;
    size_t n = strlen(s);
    while(n && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

static int ends_with_ampersand(char *s){
    size_t n = strlen(s);
    while(n && isspace((unsigned char)s[n-1])) n--;
    if(n && s[n-1] == '&'){ s[n-1] = '\0'; rstrip(s); return 1; }
    return 0;
}

static char **split_simple(const char *line){
    size_t cap = 8, n = 0;
    char **argv = malloc(sizeof(char*) * cap);
    char *tmp = strdup(line);
    char *tok = strtok(tmp, " \t");
    while(tok){
        if(n+1 >= cap){ cap *= 2; argv = realloc(argv, sizeof(char*) * cap); }
        argv[n++] = strdup(tok);
        tok = strtok(NULL, " \t");
    }
    argv[n] = NULL;
    free(tmp);
    return argv;
}

int parse_line(const char *line_in, char ***argv_out, int *background_out){
    if(!line_in || !argv_out || !background_out) return -1;
    char *line = strdup(line_in);
    rstrip(line);
    *background_out = ends_with_ampersand(line);
    *argv_out = split_simple(line);
    free(line);
    return 0;
}

void free_argv(char **argv){
    if(!argv) return;
    for(size_t i=0; argv[i]; ++i) free(argv[i]);
    free(argv);
}
