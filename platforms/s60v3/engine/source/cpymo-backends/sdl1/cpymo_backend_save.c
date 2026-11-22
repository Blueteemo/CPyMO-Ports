#ifndef S60V3
#include "../sdl2/cpymo_backend_save.c"
#else
#include "../../cpymo/cpymo_prelude.h"
#include "../include/cpymo_backend_save.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* CPyMO keeps its existing *.csav format next to PyMO's *.sav files.
 * This backend may never open PyMO's files, subdirectories or save/temp. */
static int cpymo_s60_save_name_valid(const char *name)
{
    size_t len;
    if (!name || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':'))
        return 0;
    len = strlen(name);
    return len > 5 && strcmp(name + len - 5, ".csav") == 0;
}

static FILE *cpymo_s60_open_save(const char *gamedir, const char *name, const char *mode)
{
    FILE *file;
    char *path;
    if (!gamedir || !cpymo_s60_save_name_valid(name)) {
        errno = EINVAL;
        return NULL;
    }
    path = (char *)malloc(strlen(gamedir) + strlen(name) + 8);
    if (!path) return NULL;
    sprintf(path, "%s/save/%s", gamedir, name);
    file = fopen(path, mode);
    free(path);
    return file;
}

FILE *cpymo_backend_read_save(const char *gamedir, const char *name)
{
    return cpymo_s60_open_save(gamedir, name, "rb");
}

FILE *cpymo_backend_write_save(const char *gamedir, const char *name)
{
    return cpymo_s60_open_save(gamedir, name, "wb");
}
#endif
