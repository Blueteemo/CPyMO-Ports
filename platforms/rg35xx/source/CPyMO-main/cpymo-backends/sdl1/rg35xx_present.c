#include "rg35xx_present.h"
#include "rg35xx_scale565.h"
#include <stdio.h>

int rg35xx_present(SDL_Surface *logical, SDL_Surface *lcd)
{
    if (!logical || !lcd || !logical->format || !lcd->format ||
        !logical->pixels || !lcd->pixels ||
        logical->format->BytesPerPixel != 2 ||
        lcd->format->BytesPerPixel != 2 ||
        logical->pitch < logical->w * 2 || lcd->pitch < lcd->w * 2) {
        fprintf(stderr, "[RG35XX] present: invalid framebuffer geometry or format\n");
        return -1;
    }
    if (SDL_LockSurface(logical) != 0) {
        fprintf(stderr, "[RG35XX] present: logical SDL_LockSurface failed\n");
        return -2;
    }
    if (SDL_LockSurface(lcd) != 0) {
        SDL_UnlockSurface(logical);
        fprintf(stderr, "[RG35XX] present: LCD SDL_LockSurface failed\n");
        return -3;
    }
    const int ok = rg35xx_scale16(logical->pixels, logical->w, logical->h,
        logical->pitch, lcd->pixels, lcd->w, lcd->h, lcd->pitch);
    SDL_UnlockSurface(lcd);
    SDL_UnlockSurface(logical);
    if (!ok) {
        fprintf(stderr, "[RG35XX] present: scaler rejected buffer layout\n");
        return -4;
    }
    if (SDL_Flip(lcd) != 0) {
        fprintf(stderr, "[RG35XX] present: SDL_Flip failed\n");
        return -5;
    }
    return 0;
}
