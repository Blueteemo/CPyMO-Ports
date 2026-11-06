#ifndef GARLIC_IMAGE_CACHE_H
#define GARLIC_IMAGE_CACHE_H
#ifdef GARLIC_NO_FFMPEG
#include "../cpymo-backends/include/cpymo_backend_image.h"
#include "cpymo_str.h"
#include <stdint.h>
bool garlic_image_cache_get(cpymo_backend_image *, int *, int *,
    const char *, const char *, cpymo_str, bool);
void garlic_image_cache_put(cpymo_backend_image, int, int,
    const char *, const char *, cpymo_str, bool);
void garlic_image_cache_clear(void);
void garlic_image_cache_counts(size_t *bytes, unsigned *hits, unsigned *misses);
#endif
#endif
