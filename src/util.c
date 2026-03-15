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
