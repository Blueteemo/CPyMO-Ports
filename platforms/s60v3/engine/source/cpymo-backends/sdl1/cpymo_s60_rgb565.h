#ifndef CPYMO_S60_RGB565_H
#define CPYMO_S60_RGB565_H

static int cpymo_s60_is_rgb565(const SDL_PixelFormat *format)
{
    return format->BytesPerPixel == 2 && format->Gmask == 0x07e0 &&
           (format->Rmask | format->Bmask) == 0xf81f;
}

/* Blend in the surface's own 5:6:5 bit order, using 64 alpha levels. */
static Uint16 cpymo_s60_blend565(Uint16 dst, Uint16 src, unsigned alpha)
{
    unsigned inv = 64 - alpha;
    unsigned rb = ((src & 0xf81f) * alpha + (dst & 0xf81f) * inv) >> 6;
    unsigned g = ((src & 0x07e0) * alpha + (dst & 0x07e0) * inv) >> 6;
    return (Uint16)((rb & 0xf81f) | (g & 0x07e0));
}
#endif
