#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// very minimal API; adjust names to match your main.c
// returns 0 on success, nonzero on parse error
int parse_line(const char *line, char ***argv_out, int *background_out);
void free_argv(char **argv);

// helper to join argv back into a string (for debugging)
char *argv_join(char *const argv[]);

#ifdef __cplusplus
}
#endif
