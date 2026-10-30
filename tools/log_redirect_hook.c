#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

FILE *
fopen(const char *path, const char *mode)
{
    static FILE *(*real_fopen)(const char *, const char *);
    if (!real_fopen)
        real_fopen = dlsym(RTLD_NEXT, "fopen");
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (!strcmp(name, "eden_log.txt") || !strcmp(name, "yuzu_log.txt") ||
        !strcmp(name, "eden_log.txt.old.txt") || !strcmp(name, "yuzu_log.txt.old.txt")) {
        errno = ENOENT;
        return NULL;
    }
    return real_fopen(path, mode);
}
