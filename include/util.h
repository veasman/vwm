#ifndef VWM_UTIL_H
#define VWM_UTIL_H

#include "vwm.h"

void die(const char *msg);
char *trim_whitespace(char *s);

/* command parsing */
size_t split_command_argv(
    const char *src,
    char storage[CMD_MAX_ARGS][256],
    const char **argv,
    size_t max_args
);

#endif
