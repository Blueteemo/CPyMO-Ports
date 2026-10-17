#ifndef CPYMO_RG35XX_TTF_CACHE_H
#define CPYMO_RG35XX_TTF_CACHE_H
#include <SDL/SDL_ttf.h>
#include <stdint.h>
/* Small bounded cache: same-font alternate sizes do not reinitialise TTF. */
#define RG35XX_TTF_CACHE_CAP 3
struct rg35xx_ttf_cache_slot { TTF_Font *font; int pt; uint64_t last; };
typedef struct rg35xx_ttf_cache {
    char *path;
    uint64_t age;
    struct rg35xx_ttf_cache_slot slot[RG35XX_TTF_CACHE_CAP];
} rg35xx_ttf_cache;
void rg35xx_ttf_cache_clear(rg35xx_ttf_cache *c);
/* Returns NULL without disrupting existing handles/path on failure. */
TTF_Font *rg35xx_ttf_cache_load(rg35xx_ttf_cache *c, const char *path, int pt);
TTF_Font *rg35xx_ttf_cache_size(rg35xx_ttf_cache *c, int pt);
#endif
