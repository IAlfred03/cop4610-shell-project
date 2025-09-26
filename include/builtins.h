#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_builtin(const char *cmd);
int run_builtin_parent(char *const argv[]);

#ifdef __cplusplus
}
#endif
