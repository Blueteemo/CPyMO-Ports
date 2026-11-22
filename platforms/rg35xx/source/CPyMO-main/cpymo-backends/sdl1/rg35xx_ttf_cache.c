#include "rg35xx_ttf_cache.h"
#include <stdlib.h>
#include <string.h>

void rg35xx_ttf_cache_clear(rg35xx_ttf_cache *c) {
    if (!c) return;
    for (int i = 0; i < RG35XX_TTF_CACHE_CAP; ++i) {
        if (c->slot[i].font) TTF_CloseFont(c->slot[i].font);
    }
    free(c->path);
    memset(c, 0, sizeof(*c));
}
TTF_Font *rg35xx_ttf_cache_load(rg35xx_ttf_cache *c, const char *path, int pt) {
    if (!c || !path || pt < 1 || pt > 512) return NULL;
    /* Open BEFORE changing the active cache; a bad font path is not destructive. */
    TTF_Font *font = TTF_OpenFont(path, pt);
    if (!font) return NULL;
    size_t len = strlen(path);
    if (len == SIZE_MAX) { TTF_CloseFont(font); return NULL; }
    char *copy = malloc(len + 1);
    if (!copy) { TTF_CloseFont(font); return NULL; }
    memcpy(copy, path, len + 1);
    rg35xx_ttf_cache_clear(c);
    c->path = copy;
    c->age = 1;
    c->slot[0] = (struct rg35xx_ttf_cache_slot){font, pt, 1};
    return font;
}
TTF_Font *rg35xx_ttf_cache_size(rg35xx_ttf_cache *c, int pt) {
    if (!c || !c->path || pt < 1 || pt > 512) return NULL;
    for (int i = 0; i < RG35XX_TTF_CACHE_CAP; ++i) {
        if (c->slot[i].font && c->slot[i].pt == pt) {
            c->slot[i].last = ++c->age;
            return c->slot[i].font;
        }
    }
    /* Failed font open never evicts any working cached size. */
    TTF_Font *font = TTF_OpenFont(c->path, pt);
    if (!font) return NULL;
    int victim = -1;
    for (int i = 0; i < RG35XX_TTF_CACHE_CAP; ++i) {
        if (!c->slot[i].font) { victim = i; break; }
    }
    if (victim == -1) {
        victim = 0;
        for (int i = 1; i < RG35XX_TTF_CACHE_CAP; ++i)
            if (c->slot[i].last < c->slot[victim].last) victim = i;
        TTF_CloseFont(c->slot[victim].font);
    }
    c->slot[victim] = (struct rg35xx_ttf_cache_slot){font, pt, ++c->age};
    return font;
}
