#include "util.h"
#include "wm.h"

void die(const char *msg) {
    fprintf(stderr, "fatal: %s\n", msg);
    cleanup();
    exit(1);
}

char *trim_whitespace(char *s) {
    if (!s) {
        return s;
    }

    while (*s && isspace((unsigned char)*s)) {
        s++;
    }

    if (*s == '\0') {
        return s;
    }

    char *end = s + strlen(s) - 1;

    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return s;
}

size_t split_command_argv(
    const char *src,
    char storage[CMD_MAX_ARGS][256],
    const char **argv,
    size_t max_args
) {
    if (!src || !argv || !storage || max_args == 0) {
        return 0;
    }

    for (size_t i = 0; i < max_args; i++) {
        argv[i] = NULL;
        storage[i][0] = '\0';
    }

    size_t argc = 0;
    const char *p = src;

    while (*p) {

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (!*p) {
            break;
        }

        if (argc + 1 >= max_args) {
            break;
        }

        char *dst = storage[argc];
        size_t di = 0;

        bool in_single = false;
        bool in_double = false;

        while (*p) {

            unsigned char ch = (unsigned char)*p;

            if (!in_single && !in_double && isspace(ch)) {
                break;
            }

            if (!in_double && ch == '\'') {
                in_single = !in_single;
                p++;
                continue;
            }

            if (!in_single && ch == '"') {
                in_double = !in_double;
                p++;
                continue;
            }

            if (ch == '\\') {
                p++;
                if (!*p) {
                    break;
                }
                ch = (unsigned char)*p;
            }

            if (di + 1 < 256) {
                dst[di++] = (char)ch;
            }

            p++;
        }

        dst[di] = '\0';

        if (dst[0] != '\0') {
            argv[argc++] = dst;
        }

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
    }

    argv[argc] = NULL;

    return argc;
}
